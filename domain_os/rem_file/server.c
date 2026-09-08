/*
 * REM_FILE_$SERVER - remote file operation server
 *
 * Original address: 0x00E63586, 4108 bytes.
 * Module base A5 = 0x00E823FC (`lea (0xe823fc).l,A5` at 0x00E6358E); the only
 * A5 global this function touches is the longword at A5+0
 * (REM_FILE_$STALE_LINK_COUNT).
 *
 * One call handles exactly one request: receive a packet on socket 2, parse
 * the header, dispatch on the opcode (delegating two whole ranges to
 * DIR_$SERVER and ACL_$SERVER), then build and send the reply.  The caller
 * (0x00E11AC2) loops.
 *
 * Frame (link.w A6,-0x4E4).  Every field below keeps the A6 displacement the
 * listing uses; `rem_file_server_frame_t` is laid out so that
 * `(uint8_t *)&frame + 0x4E4 + <A6 displacement>` addresses it, and the
 * request/response accessor macros take the displacement directly so each
 * line can be checked against the disassembly.
 *
 * The Pascal source declared eight nested procedures.  They reach this frame
 * through the static link (`movea.l (A6),A2` etc.) and are flattened here
 * into static helpers taking an explicit frame pointer:
 *
 *   0x00E62CEA  REM_FILE_$SERVER__unmap_name
 *   0x00E62D4A  REM_FILE_$SERVER__get_entry
 *   0x00E62DE8  REM_FILE_$SERVER__set_attribute
 *   0x00E62F54  REM_FILE_$SERVER__get_entry_sids
 *   0x00E63096  REM_FILE_$SERVER__drop_link
 *   0x00E631C2  REM_FILE_$SERVER__truncate_delete
 *   0x00E632C2  REM_FILE_$SERVER__generate_uid
 *   0x00E63344  REM_FILE_$SERVER__set_prot_attrib
 */

#include "rem_file/rem_file_internal.h"
#include "file/file.h"
#include "name/name.h"
#include "dir/dir.h"
#include "area/area.h"
#include "ml/ml.h"
#include "app/app.h"
#include "netbuf/netbuf.h"
#include "pkt/pkt.h"
#include "audit/audit.h"
#include "misc/crash_system.h"
#include "os/os.h"
#include "rgyc/rgyc.h"
#include "slink/slink.h"
#include "time/time.h"
#include "netlog/netlog.h"

/*
 * ============================================================================
 * Constants
 * ============================================================================
 */

/* Socket the server listens on (`move.w #0x2,-(SP)` at 0x00E635B0 and again
 * as PKT_$SEND_INTERNET's source socket at 0x00E6423C). */
#define REM_FILE_SERVER_SOCK        2

/* Largest request body OS_$DATA_COPY will move into the frame
 * (`cmpi.w #0x294,D1w` at 0x00E63682). */
#define REM_FILE_REQUEST_MAX        0x294

/* Largest reply header PKT_$SEND_INTERNET is given in one go; anything longer
 * spills into the bulk-data pointer (0x00E64168). */
#define REM_FILE_REPLY_SPLIT        0x200

/* Largest bulk transfer (0x00E64204 and 0x00E636EA). */
#define REM_FILE_BULK_MAX           0x400

/*
 * How far past A6 the split-reply bulk area reaches.
 *
 * The reply record starts at A6-0x1A0 and 0x00E6417C takes the bulk pointer
 * as `lea (0x60,A6),A4` - reply + 0x200 - which is 0x60 bytes ABOVE A6, in
 * the caller's stack.  REM_FILE_$SERVER is reached by `jsr` with no arguments
 * at all (0x00E11AC2), so what lies there is NETWORK_$REQUEST_SERVER's own
 * frame: that function's `link.w A6,-0xe0` plus its nine-register `movem`
 * (0x24 bytes) put its stack pointer at the call at A6_caller-0x104, so
 *
 *   A6_callee      = A6_caller - 0x10C     (return address + the link)
 *   A6_callee+0x60 = A6_caller - 0x0AC
 *
 * which is a longword local NETWORK_$REQUEST_SERVER writes at 0x00E11A68 /
 * 0x00E11A72.  Nothing coordinates the two uses: this is the original
 * overrunning its own frame into its caller's, and the only faithful C model
 * is to keep that memory inside the same object, which `caller_frame` below
 * does.  It has to reach reply+0x200+REM_FILE_BULK_MAX, i.e. frame offset
 * 0x344 + 0x200 + 0x400 = 0x944, so 0x944 - 0x4E4 bytes.
 */
#define REM_FILE_SPILL_BYTES        0x460

/* Response header marker written at 0x00E637E4. */
#define REM_FILE_RESPONSE_MAGIC     0x80

/* Opcode ranges delegated wholesale (0x00E63840 / 0x00E6395E). */
#define REM_FILE_DIR_OP_FIRST       0x2A
#define REM_FILE_DIR_OP_LAST        0x5C
#define REM_FILE_ACL_OP_FIRST       0x64
#define REM_FILE_ACL_OP_LAST        0x77

/* Status constants raised by this function. */
#define rem_file_$rcv_queue_empty       0x00110006  /* 0x00E635BE */
#define rem_file_$service_not_enabled   0x0011000F  /* 0x00E63804 */
#define rem_file_$reply_too_long        0x00110001  /* 0x00E6420A */
#define rem_file_$request_too_long      0x0011001C  /* 0x00E6375A */

/* Bit 1 of NETWORK_$CAPABLE_FLAGS gates the whole service (0x00E637FA). */
#define NETWORK_CAP_FILE_SERVER         0x02

/* NETLOG kind 0x15 and the tick divisor used for the two latency figures
 * (`divs.w #0xfa` at 0x00E644FC / 0x00E6450A). */
#define REM_FILE_LOG_KIND           0x15
#define REM_FILE_LOG_TICKS_PER_UNIT 250

/*
 * Server opcodes: the request byte at A6-0x435 ("move.b (-0x435,A6),D0b" -
 * the request record is based at A6-0x438 and the opcode is its byte +0x03).
 * Every code a REM_FILE_$* builder can send is defined once, in
 * rem_file/rem_file_internal.h (bead source-8joj); only the four DIR_$SERVER
 * codes that REM_FILE_$SERVER has to special-case for their netbuf are local
 * to this file, because no REM_FILE_$ builder produces them.
 */
#define SERVER_OP_DIR_GET_ENTRY     0x3C    /* DIR_$SERVER, needs a netbuf */
#define SERVER_OP_DIR_READ_LINK     0x3E    /* DIR_$SERVER, needs a netbuf */
#define SERVER_OP_DIR_READ_DIR      0x42    /* DIR_$SERVER, needs a netbuf */
#define SERVER_OP_DIR_LIST          0x58    /* DIR_$SERVER, needs a netbuf */

/*
 * ============================================================================
 * Frame layout
 * ============================================================================
 */

typedef struct rem_file_server_frame_t {
    void       *lock_acl_cell;      /* 0x000: -0x4E4, holds &req.arg[0x30] */
    uint8_t     pad_004[0x0C];      /* 0x004 */
    uint8_t     trunc_result[2];    /* 0x010: -0x4D4, AST_$TRUNCATE result */
    uint16_t    lock_scan_index;    /* 0x012: -0x4D2 */
    uint16_t    pad_014;            /* 0x014 */
    uint16_t    pkt_dest_sock;      /* 0x016: -0x4CE, rcv+0x12 */
    uint16_t    pkt_request_id;     /* 0x018: -0x4CC, rcv+0x06 */
    uint16_t    request_len;        /* 0x01A: -0x4CA */
    uint16_t    reply_len;          /* 0x01C: -0x4C8 */
    uint16_t    reply_hdr_len;      /* 0x01E: -0x4C6 */
    uint16_t    pad_020;            /* 0x020: -0x4C4, unreferenced */
    uint16_t    tmp_index;          /* 0x022: -0x4C2, backlog index and the
                                     *        unmap_name copy counter */
    uint16_t    uid_retry;          /* 0x024: -0x4C0 (generate_uid;
                                     *        `clr.w (-0x4c0,A2)` 0x00E632CE) */
    uint16_t    send_len_out;       /* 0x026: -0x4BE */
    uint16_t    send_extra_out;     /* 0x028: -0x4BC */
    uint16_t    seg_map_flag;       /* 0x02A: -0x4BA */
    uint16_t    lock_flags;         /* 0x02C: -0x4B8 */
    uint16_t    create_flags;       /* 0x02E: -0x4B6 */
    uint32_t    pkt_src_node;       /* 0x030: -0x4B4, rcv+0x08 */
    uint32_t    pkt_src_override;   /* 0x034: -0x4B0, rcv+0x18 */
    uint32_t    crash_node;         /* 0x038: -0x4AC */
    uint32_t    pkt_routing_key;    /* 0x03C: -0x4A8, rcv+0x1C */
    uint32_t    zero_long;          /* 0x040: -0x4A4 */
    uint16_t    lock_slot;          /* 0x044: -0x4A0, FILE_$PRIV_LOCK slot */
    uint16_t    pad_046;            /* 0x046 */
    uint32_t    netbuf_va;          /* 0x048: -0x49C */
    uint32_t    pad_04c;            /* 0x04C */
    status_$t   local_status;       /* 0x050: -0x494 (low word at -0x492) */
    uint8_t     clock_recv[6];      /* 0x054: -0x490, low long at -0x48E */
    uint8_t     pad_05a[2];         /* 0x05A */
    uint8_t     clock_start[6];     /* 0x05C: -0x488, low long at -0x486 */
    uint8_t     pad_062[2];         /* 0x062 */
    uint8_t     clock_end[6];       /* 0x064: -0x480, low long at -0x47E */
    uint8_t     pad_06a[2];         /* 0x06A */
    union {
        uint32_t    node;           /* 0x06C: -0x478 as a longword */
        struct {
            uint8_t     log_op;     /* 0x06C: -0x478 */
            uint8_t     log_nibble; /* 0x06D: -0x477 */
            uint16_t    log_low;    /* 0x06E: -0x476 */
        } f;
        uint16_t    w[2];
    } log;                          /* 0x06C */
    uint32_t    netbuf_hdr_addr;    /* 0x070: -0x474 */
    uint8_t     scratch_070[0x0C];  /* 0x074: -0x470 AST_$GET_LOCATION output
                                     *        -0x46C FILE_$PRIV_UNLOCK DTV out */
    status_$t   dir_status;         /* 0x080: -0x464 */
    file_lock_info_internal_t lock_info;  /* 0x084: -0x460, 34 bytes */
    uint8_t     pad_0a6[6];         /* 0x0A6 */
    rem_file_server_req_t   request;      /* 0x0AC: -0x438 */
    rem_file_server_resp_t  response;     /* 0x344: -0x1A0 */
    uint32_t    netbuf_hdr[4];      /* 0x464: -0x80, copy of rcv+0x08..0x17 */
    uint8_t     pad_474[0x40];      /* 0x474: -0x70 */
    rem_file_rcv_t rcv;             /* 0x4B4: -0x30, APP_$RECEIVE result */
    /* ---- past A6: the caller's frame, see REM_FILE_SPILL_BYTES ---- */
    uint8_t     caller_frame[REM_FILE_SPILL_BYTES]; /* 0x4E4: A6+0x00 */
} rem_file_server_frame_t;

#if defined(ARCH_M68K)
_Static_assert(offsetof(rem_file_server_frame_t, request)  == 0x0AC,
               "frame.request at A6-0x438");
_Static_assert(offsetof(rem_file_server_frame_t, response) == 0x344,
               "frame.response at A6-0x1A0");
_Static_assert(offsetof(rem_file_server_frame_t, response) -
               offsetof(rem_file_server_frame_t, request) == 0x298,
               "request and response are 0x298 apart");
_Static_assert(offsetof(rem_file_server_frame_t, request_len) == 0x01A,
               "frame.request_len at A6-0x4CA");
_Static_assert(offsetof(rem_file_server_frame_t, reply_len)   == 0x01C,
               "frame.reply_len at A6-0x4C8");
_Static_assert(offsetof(rem_file_server_frame_t, local_status) == 0x050,
               "frame.local_status at A6-0x494");
_Static_assert(offsetof(rem_file_server_frame_t, lock_info) == 0x084,
               "frame.lock_info at A6-0x460");
_Static_assert(offsetof(rem_file_server_frame_t, netbuf_hdr) == 0x464,
               "frame.netbuf_hdr at A6-0x80");
_Static_assert(offsetof(rem_file_server_frame_t, rcv) == 0x4B4,
               "frame.rcv at A6-0x30");
_Static_assert(sizeof(rem_file_rcv_t) == 0x30, "sizeof receive block");
_Static_assert(offsetof(rem_file_rcv_t, bufs)  == 0x08, "rcv.bufs at -0x28");
_Static_assert(offsetof(rem_file_rcv_t, clock) == 0x20, "rcv.clock at -0x10");
_Static_assert(offsetof(rem_file_rcv_t, f_26)  == 0x26, "rcv.f_26 at -0x0A");
_Static_assert(sizeof(rem_file_server_req_t)  == 0x298, "sizeof request");
_Static_assert(sizeof(rem_file_server_resp_t) == 0x120, "sizeof response");
_Static_assert(offsetof(rem_file_server_frame_t, caller_frame) == 0x4E4,
               "the frame proper ends at A6+0x00");
_Static_assert(offsetof(rem_file_server_frame_t, response) + 0x200 <
               sizeof(rem_file_server_frame_t),
               "the split-reply bulk area must be inside the object");
#endif

/*
 * Field accessors.  The second argument is the A6 displacement printed in the
 * listing, so every use is directly checkable against the disassembly.
 */
#define REQ_P(f, a6)  ((void *)((uint8_t *)&(f)->request + (0x438 + (a6))))
#define REQ_B(f, a6)  (*(uint8_t  *)REQ_P(f, a6))
#define REQ_W(f, a6)  (*(uint16_t *)REQ_P(f, a6))
#define REQ_L(f, a6)  (*(uint32_t *)REQ_P(f, a6))

#define RSP_P(f, a6)  ((void *)((uint8_t *)&(f)->response + (0x1A0 + (a6))))
#define RSP_B(f, a6)  (*(uint8_t  *)RSP_P(f, a6))
#define RSP_W(f, a6)  (*(uint16_t *)RSP_P(f, a6))
#define RSP_L(f, a6)  (*(uint32_t *)RSP_P(f, a6))

/* The 48-bit clocks: the low longword sits two bytes into the record. */
#define CLOCK_LO(c)   (*(uint32_t *)(void *)((uint8_t *)(c) + 2))

/*
 * ============================================================================
 * Nested helpers
 * ============================================================================
 */

static void server_unmap_name(rem_file_server_frame_t *f,
                              char *name, int16_t *name_len);
static void server_get_entry(rem_file_server_frame_t *f,
                             rem_file_server_req_t *req,
                             rem_file_server_resp_t *resp);
static void server_set_attribute(rem_file_server_frame_t *f);
static void server_get_entry_sids(rem_file_server_frame_t *f);
static void server_drop_link(rem_file_server_frame_t *f);
static void server_truncate_delete(rem_file_server_frame_t *f);
static void server_generate_uid(rem_file_server_frame_t *f);
static void server_set_prot_attrib(rem_file_server_frame_t *f);

/*
 * REM_FILE_$SERVER__unmap_name (0x00E62CEA)
 *
 * Rewrites `name` in place from the network's case-mapped form.  The copy
 * loop counter lives in the PARENT frame at A6-0x4C2 (0x00E62D24), which is
 * the same cell the receive path used for the backlog index.
 */
static void server_unmap_name(rem_file_server_frame_t *f,
                              char *name, int16_t *name_len)
{
    char     converted[0x20];       /* A6-0x28 */
    int16_t  converted_len;         /* A6-0x2A */
    uint8_t  truncated;             /* A6-0x2C */
    int16_t  remaining;

    UNMAP_CASE(name, name_len, converted, (int16_t *)&REM_FILE_$MAX_NAME_LEN,
               &converted_len, &truncated);

    *name_len = converted_len;                  /* 0x00E62D1E */

    /* 0x00E62D20: subq.w #1 + bmi + dbf => converted_len iterations. */
    remaining = converted_len - 1;
    if (remaining < 0) {
        return;
    }
    f->tmp_index = 1;                           /* 0x00E62D24 */
    do {
        name[f->tmp_index - 1] = converted[f->tmp_index - 1];
        f->tmp_index++;
    } while (remaining-- != 0);
}

/*
 * REM_FILE_$SERVER__get_entry (0x00E62D4A)
 *
 * Looks a name up in a directory and maps the answer back to the network's
 * case convention.
 */
static void server_get_entry(rem_file_server_frame_t *f,
                             rem_file_server_req_t *req,
                             rem_file_server_resp_t *resp)
{
    uint8_t *rq = (uint8_t *)req;
    uint8_t *rp = (uint8_t *)resp;
    int16_t  entry_type;            /* A6-0x18 */
    uint8_t  entry_info[12];        /* A6-0x16 */
    uint8_t  truncated;             /* A6-0x1A */

    /* 0x00E62D5C: request +0x0C is the name, +0x2C its length. */
    server_unmap_name(f, (char *)(rq + 0x0C), (int16_t *)(rq + 0x2C));

    /* 0x00E62D6C */
    DIR_$GET_ENTRYU((uid_t *)(rq + 0x04), (char *)(rq + 0x0C),
                    (uint16_t *)(rq + 0x2C), &entry_type,
                    (status_$t *)(rp + 0x04));

    /* 0x00E62D8A: only the LOW word of the status is tested. */
    if (*(uint16_t *)(rp + 0x06) != 0) {
        return;
    }

    /* 0x00E62D90 */
    MAP_CASE((char *)(rq + 0x0C), (int16_t *)(rq + 0x2C), (char *)(rp + 0x0C),
             (int16_t *)&REM_FILE_$MAX_NAME_LEN, (int16_t *)(rp + 0x0A),
             &truncated);

    if ((int8_t)truncated < 0) {                /* 0x00E62DB2 */
        *(uint16_t *)(rp + 0x0A) = 0x20;
        *(status_$t *)(rp + 0x04) = 0x000E002D;
    }

    *(uint16_t *)(rp + 0x08) = (uint16_t)entry_type;    /* 0x00E62DC6 */

    /* 0x00E62DCC: moveq #0xB + dbf => 12 bytes. */
    for (int i = 0; i < 12; i++) {
        rp[0x2C + i] = entry_info[i];
    }
}

/*
 * REM_FILE_$SERVER__set_attribute (0x00E62DE8) - opcode 0x04
 */
static void server_set_attribute(rem_file_server_frame_t *f)
{
    status_$t status;               /* A6-0x7C */
    /* A6-0x78: the 0x38-byte record AST_$GET_ACL_ATTRIBUTES fills; only its
     * first byte (0x00E62E8C) and default_acl are read here. */
    ast_$acl_attr_t acl_attrs;
    uint32_t  acl_data[11];         /* A6-0x6C */
    uint8_t   spare[0x2C];          /* A6-0x40 */
    uid_t     converted;            /* A6-0x10 */
    uid_t     old_acl;              /* A6-0x38 */
    file_$obj_loc_t desc;           /* A6-0x30 */
    uid_t    *file_uid  = &f->request.uid;              /* A6-0x434 */
    uint16_t  attr_type = REQ_W(f, -0x42C);
    uint8_t  *attr_val  = (uint8_t *)REQ_P(f, -0x42A);
    uint16_t  send_type = attr_type;

    f->reply_len = 8;                                   /* 0x00E62DF2 */

    if (attr_type == 3) {
        /* 0x00E62E06: bits 4..11 of the word at request +0x16. */
        if (((REQ_W(f, -0x426) & 0x0FF0) >> 4) & 0xE0) {
            /* 0x00E62E16 */
            ACL_$CONVERT_FUNKY_ACL(attr_val, acl_data, &converted,
                                   spare, &status);
            *(uint32_t *)attr_val       = converted.high;
            *(uint32_t *)(attr_val + 4) = converted.low;
            f->response.status = status;
            attr_val[4] &= (uint8_t)~0x01;              /* 0x00E62E46 */
        } else {
            /* 0x00E62E50 */
            desc.uid = *file_uid;
            desc.flags &= (int8_t)~FILE_OBJ_LOC_SCRATCH;
            AST_$GET_ACL_ATTRIBUTES(&desc, 1, &acl_attrs, &status);
            f->response.status = status;
            if (f->response.status != status_$ok) {
                return;                                 /* 0x00E62E88 */
            }
            /* 0x00E62E8C: an object that is not already a 10-ACL goes
             * through unconverted, with its original attribute id. */
            if (acl_attrs.obj_flags[0] == 0) {
                goto set_attribute;                     /* 0x00E62F2C */
            }
            old_acl.high = *(uint32_t *)attr_val;
            old_acl.low  = *(uint32_t *)(attr_val + 4);
            (void)ACL_$CONVERT_TO_10ACL(&old_acl, file_uid, &converted,
                                        acl_data, &status);
            *(uint32_t *)attr_val       = converted.high;
            *(uint32_t *)(attr_val + 4) = converted.low;
            f->response.status = status;
        }

        /* 0x00E62ED4 */
        if (f->response.status != status_$ok) {
            return;
        }
        REQ_L(f, -0x3FE) = *(uint32_t *)attr_val;
        REQ_L(f, -0x3FA) = *(uint32_t *)(attr_val + 4);
        for (int i = 0; i < 11; i++) {                  /* moveq #0xA + dbf */
            ((uint32_t *)(void *)attr_val)[i] = acl_data[i];
        }
        /* 0x00E62F00: 0x14 is passed to AST_$SET_ATTRIBUTE without being
         * written back into the request. */
        send_type = 0x14;
    } else if (attr_type == 4) {
        /* 0x00E62F0E: retyping an object to "symbolic link" is refused. */
        if ((*(uint32_t *)attr_val == SLINK_$UID.high) &&
            (*(uint32_t *)(attr_val + 4) == SLINK_$UID.low)) {
            status = file_$incompatible_request;
            f->response.status = status;                /* 0x00E62F44 */
            return;
        }
    }

set_attribute:                                          /* 0x00E62F2C */
    AST_$SET_ATTRIBUTE(file_uid, send_type, attr_val, &status);
    f->response.status = status;
}

/*
 * REM_FILE_$SERVER__get_entry_sids (0x00E62F54) - opcode 0x1C
 *
 * Protocol version 0x32 looks the entry up directly; anything newer runs the
 * lookup under the caller's SIDs and project list.
 */
static void server_get_entry_sids(rem_file_server_frame_t *f)
{
    f->reply_len = 0x38;

    if (f->request_len == 0x32) {               /* 0x00E62F62 */
        server_get_entry(f, &f->request, &f->response);
        return;
    }

    {
        boolean   sids_set = 0;                 /* D2 */
        boolean   proj_set = 0;                 /* D3 */
        uint8_t   saved_sids1[40];              /* A6-0xB8 */
        uint8_t   saved_sids2[40];              /* A6-0x90 */
        uint8_t   saved_proj1[16];              /* A6-0x68 */
        uint8_t   saved_proj2[16];              /* A6-0x58 */
        uint8_t   saved_proj_list[0x48];        /* A6-0x48 */
        uint8_t   saved_proj_count[2];          /* A6-0xBE */
        status_$t temp_status;

        ACL_$ENTER_SUPER();                     /* 0x00E62F80 */
        AUDIT_$SUSPEND();

        ACL_$GET_RE_ALL_SIDS(saved_sids1, (uid_t *)(void *)saved_sids2,
                             saved_proj1, (int32_t *)(void *)saved_proj2,
                             &f->response.status);
        if (f->response.status != status_$ok) goto restore;

        ACL_$GET_PROJ_LIST((uid_t *)(void *)saved_proj_list,
                           (int16_t *)&REM_FILE_$MAX_PROJ_LIST,
                           (int16_t *)(void *)saved_proj_count,
                           &f->response.status);
        if (f->response.status != status_$ok) goto restore;

        /* 0x00E62FDE: the caller's SIDs arrive at request +0x34. */
        ACL_$SET_RE_ALL_SIDS(saved_sids1, REQ_P(f, -0x404), saved_proj1,
                             saved_proj2, &f->response.status);
        if (f->response.status != status_$ok) goto restore;
        sids_set = -1;

        /* 0x00E62FFC: caller's project list at request +0x58, count +0x98. */
        ACL_$SET_PROJ_LIST((uid_t *)REQ_P(f, -0x3E0),
                           (int16_t *)REQ_P(f, -0x3A0),
                           &f->response.status);
        if (f->response.status != status_$ok) goto restore;
        proj_set = -1;

        AUDIT_$RESUME();                        /* 0x00E63016 */
        ACL_$EXIT_SUPER();

        server_get_entry(f, &f->request, &f->response);

        ACL_$ENTER_SUPER();
        AUDIT_$SUSPEND();

restore:
        if (sids_set < 0) {
            ACL_$SET_RE_ALL_SIDS(saved_sids1, saved_sids2, saved_proj1,
                                 saved_proj2, &temp_status);
        }
        if (proj_set < 0) {
            ACL_$SET_PROJ_LIST((uid_t *)(void *)saved_proj_list,
                               (int16_t *)(void *)saved_proj_count,
                               &temp_status);
        }
        AUDIT_$RESUME();
        ACL_$EXIT_SUPER();
    }
}

/*
 * REM_FILE_$SERVER__drop_link (0x00E63096) - opcode 0x28
 */
static void server_drop_link(rem_file_server_frame_t *f)
{
    boolean   as_locksmith = (boolean)REQ_B(f, -0x406);
    uint8_t   saved_sids1[40];
    uint8_t   saved_sids2[40];
    uint8_t   saved_proj1[16];
    uint8_t   saved_proj2[16];
    status_$t temp_status;

    /* 0x00E630xx: request +0x20 is the name, +0x40 its length. */
    server_unmap_name(f, (char *)REQ_P(f, -0x42C), (int16_t *)REQ_P(f, -0x40C));

    if (as_locksmith < 0) {
        ACL_$ENTER_SUPER();
        AUDIT_$SUSPEND();

        ACL_$OVERRIDE_LOCAL_LOCKSMITH(-1, &f->response.status);
        if (f->response.status != status_$ok) goto restore_super;

        ACL_$GET_RE_ALL_SIDS(saved_sids1, (uid_t *)(void *)saved_sids2,
                             saved_proj1, (int32_t *)(void *)saved_proj2,
                             &f->response.status);
        if (f->response.status != status_$ok) goto restore_super;

        ACL_$SET_RE_ALL_SIDS(saved_sids2, saved_sids2, saved_proj1, saved_proj2,
                             &f->response.status);
        if (f->response.status != status_$ok) goto restore_super;
    }

    DIR_$OLD_DROP_HARD_LINKU(&f->request.uid, (char *)REQ_P(f, -0x42C),
                             (uint16_t *)REQ_P(f, -0x40C),
                             (uint16_t *)REQ_P(f, -0x40A), &f->response.status);

    if (as_locksmith < 0) {
        ACL_$SET_RE_ALL_SIDS(saved_sids1, saved_sids2, saved_proj1, saved_proj2,
                             &temp_status);
        ACL_$OVERRIDE_LOCAL_LOCKSMITH(0, &temp_status);
    }

restore_super:
    if (as_locksmith < 0) {
        AUDIT_$RESUME();
        ACL_$EXIT_SUPER();
    }

    if (f->response.status == status_$naming_directory_locked) {
        f->response.pkt_flag = 0xFFFF;
        REM_FILE_$STALE_LINK_COUNT++;
    }

    f->reply_len = 8;
}

/*
 * REM_FILE_$SERVER__truncate_delete (0x00E631C2) - opcode 0x08
 */
static void server_truncate_delete(rem_file_server_frame_t *f)
{
    uid_t   *file_uid = &f->request.uid;
    boolean  do_delete = (boolean)REQ_B(f, -0x42C);
    /* A6-0x104 is the 32-byte object descriptor whose UID lives at +8; it
     * overlaps the reply payload, exactly as in the original. */
    file_$obj_loc_t *desc = (file_$obj_loc_t *)RSP_P(f, -0x104);
    uint8_t *attrs = (uint8_t *)RSP_P(f, -0x194);

    f->response.status = status_$ok;

    desc->uid = *file_uid;
    desc->flags &= (int8_t)~FILE_OBJ_LOC_SCRATCH;

    if ((do_delete < 0) && (f->request.reserved_02 == 0)) {
        AST_$GET_ATTRIBUTES(desc, 0x81, attrs, &f->response.status);
        if ((f->response.status == status_$ok) && (attrs[0] == 0)) {
            uint16_t one = 1;
            AST_$SET_ATTRIBUTE(file_uid, 7, &one, &f->response.status);
        }
    }

    f->reply_len = 8;

    if ((f->response.status == status_$ok) ||
        (f->response.status == 0x00030007)) {
        if (do_delete < 0) {
            FILE_$DELETE(file_uid, &f->response.status);
        } else {
            uint8_t trunc_result[8];

            AST_$TRUNCATE(file_uid, REQ_L(f, -0x42A), 0, trunc_result,
                          &f->response.status);
            AST_$GET_ATTRIBUTES(desc, 0x80, attrs,
                                &f->response.status);
            RSP_L(f, -0x198) = *(uint32_t *)(attrs + 0x38);
            RSP_W(f, -0x194) = *(uint16_t *)(attrs + 0x3C);
            f->reply_len = 0x10;
        }
    }
}

/*
 * REM_FILE_$SERVER__generate_uid (0x00E632C2) - opcode 0x24
 */
static void server_generate_uid(rem_file_server_frame_t *f)
{
    /*
     * 0x00E632D6: UID_$GEN writes straight into the reply body at
     * A3-0x198, so the generated UID never lives in a local of its own.
     */
    uid_t          *generated = (uid_t *)RSP_P(f, -0x198);
    file_$obj_loc_t probe;              /* A6-0x28 in the nested frame */
    uint32_t        vol_uid;            /* A6-0x2C: receives aote+0x08 */

    f->uid_retry = 0;

    do {
        f->uid_retry++;
        UID_$GEN(generated);

        /* Seed the record's UID at +0x08 and clear bit 6 of +0x1D */
        probe.flags &= (int8_t)~FILE_OBJ_LOC_SCRATCH;   /* 0x00E632E2 */
        probe.uid = *generated;                         /* 0x00E632E8 */

        /*
         * 0x00E632FE hands the routine the PARENT frame's scratch cell at
         * A2-0x470 as the argument it never touches, and a local longword
         * at A6-0x2C as the aote+0x08 output.
         */
        AST_$GET_LOCATION(&probe, 1,
                          (uint32_t *)(void *)f->scratch_070, &vol_uid,
                          &f->response.status);

        if (f->response.status == file_$object_not_found) {
            break;
        }
    } while (f->uid_retry <= 10);

    if (f->response.status == file_$object_not_found) {
        f->response.status = status_$ok;
    }

    f->reply_len = 0x12;
}

/*
 * REM_FILE_$SERVER__set_prot_attrib (0x00E63344) - opcodes 0x80 and 0x82
 */
static void server_set_prot_attrib(rem_file_server_frame_t *f)
{
    uint8_t   opcode = f->request.opcode;
    boolean   sids_set = 0;
    boolean   proj_set = 0;
    uint8_t   saved_sids1[40];
    uint8_t   saved_sids2[40];
    uint8_t   saved_proj1[16];
    uint8_t   saved_proj2[16];
    uint8_t   saved_proj_list[0x48];
    uint8_t   saved_proj_count[2];
    status_$t temp_status;
    uint16_t  prot_type = 0;

    f->reply_len = 0xBE;

    if (opcode == REM_FILE_OP_FILE_SET_PROT) {
        switch (REQ_W(f, -0x42A)) {
        case 0x03:  prot_type = 6; break;
        case 0x10:  prot_type = 0; break;
        case 0x11:  prot_type = 1; break;
        case 0x12:  prot_type = 2; break;
        case 0x13:  prot_type = 4; break;
        case 0x14:  prot_type = 5; break;
        case 0x15:  prot_type = 3; break;
        default:                   break;
        }
    }

    ACL_$ENTER_SUPER();
    AUDIT_$SUSPEND();

    ACL_$GET_RE_ALL_SIDS(saved_sids1, (uid_t *)(void *)saved_sids2,
                         saved_proj1, (int32_t *)(void *)saved_proj2,
                         &f->response.status);
    if (f->response.status != status_$ok) goto restore;

    ACL_$GET_PROJ_LIST((uid_t *)(void *)saved_proj_list,
                       (int16_t *)&REM_FILE_$MAX_PROJ_LIST,
                       (int16_t *)(void *)saved_proj_count,
                       &f->response.status);
    if (f->response.status != status_$ok) goto restore;

    ACL_$SET_RE_ALL_SIDS(saved_sids1,
                         (opcode == REM_FILE_OP_FILE_SET_PROT) ? REQ_P(f, -0x3F4)
                                                        : REQ_P(f, -0x3F0),
                         saved_proj1, saved_proj2, &f->response.status);
    if (f->response.status != status_$ok) goto restore;
    sids_set = -1;

    ACL_$SET_PROJ_LIST((uid_t *)((opcode == REM_FILE_OP_FILE_SET_PROT)
                                 ? REQ_P(f, -0x3D0) : REQ_P(f, -0x3CC)),
                       (int16_t *)&REM_FILE_$MAX_PROJ_LIST,
                       &f->response.status);
    if (f->response.status != status_$ok) goto restore;
    proj_set = -1;

    AUDIT_$RESUME();
    ACL_$EXIT_SUPER();

    if (opcode == REM_FILE_OP_FILE_SET_PROT) {
        /* 0x00E634BA `move.b (-0x42c,A2),-(SP)`: subsys_flag is a BYTE, not
         * the word this used to read (bead source-w7lk). */
        FILE_$SET_PROT_INT(&f->request.uid, REQ_P(f, -0x428),
                           REQ_W(f, -0x42A), prot_type,
                           (boolean)REQ_B(f, -0x42C), &f->response.status);
    } else {
        /* 0x00E634D6-0x00E634F6: six pushes - uid, attr word (-0x42A),
         * value (-0x424), rights word (-0x426), option word (-0x42C),
         * status. */
        FILE_$SET_ATTRIBUTE(&f->request.uid, (int16_t)REQ_W(f, -0x42A),
                            REQ_P(f, -0x424),
                            REQ_W(f, -0x426),
                            (int16_t)REQ_W(f, -0x42C),
                            &f->response.status);
    }

    if (f->response.status == status_$ok) {
        file_$obj_loc_t *desc = (file_$obj_loc_t *)RSP_P(f, -0x104);

        desc->uid = f->request.uid;
        AST_$GET_ATTRIBUTES(desc, 0x81, RSP_P(f, -0x194),
                            &f->response.status);
    }

    ACL_$ENTER_SUPER();
    AUDIT_$SUSPEND();

restore:
    if (sids_set < 0) {
        ACL_$SET_RE_ALL_SIDS(saved_sids1, saved_sids2, saved_proj1, saved_proj2,
                             &temp_status);
    }
    if (proj_set < 0) {
        ACL_$SET_PROJ_LIST((uid_t *)(void *)saved_proj_list,
                           (int16_t *)(void *)saved_proj_count, &temp_status);
    }
    AUDIT_$RESUME();
    ACL_$EXIT_SUPER();
}

/*
 * ============================================================================
 * Log-code mapping (0x00E64294-0x00E644F2)
 * ============================================================================
 */
static uint8_t server_log_code(uint8_t opcode)
{
    switch (opcode) {
    case 0x00: return 0x0B;
    case 0x02: return 0x12;
    case 0x04: return 0x01;
    case 0x06: return 0x00;
    case 0x08: return 0x03;
    case 0x0A: return 0x04;
    case 0x0C: return 0x05;
    case 0x0E: return 0x09;
    case 0x10: return 0x02;
    case 0x12: return 0x06;
    case 0x14: return 0x0A;
    case 0x16: return 0x07;
    case 0x18: return 0x24;
    case 0x1A: return 0x08;
    case 0x1C: return 0x0D;
    case 0x1E: return 0x0E;
    case 0x20: return 0x0F;
    case 0x7C: return 0x3D;
    case 0x22: return 0x10;
    case 0x62: return 0x11;
    case 0x86: return 0x3A;
    case 0x88: return 0x3B;
    case 0x8A: return 0x3C;
    case 0x7E: return 0x3E;
    case 0x80: return 0x3F;
    case 0x82: return 0x40;
    case 0x84: return 0x41;
    case 0x24: return 0x42;
    case 0x28: return 0x1E;
    default:
        /* 0x00E6449A: the two delegated ranges are folded down to a pair of
         * dense ranges; the `bpl / addq.l #1 / asr.l #1` sequence is a signed
         * divide by two, and both operands are non-negative here. */
        if ((opcode >= REM_FILE_DIR_OP_FIRST) && (opcode <= REM_FILE_DIR_OP_LAST)) {
            return (uint8_t)(((opcode - REM_FILE_DIR_OP_FIRST) / 2) + 0x13);
        }
        if ((opcode >= REM_FILE_ACL_OP_FIRST) && (opcode <= REM_FILE_ACL_OP_LAST)) {
            return (uint8_t)(((opcode - REM_FILE_ACL_OP_FIRST) / 2) + 0x2F);
        }
        /* 0x00E6428E already set 0x12 and nothing overwrites it. */
        return 0x12;
    }
}

/*
 * ============================================================================
 * REM_FILE_$SERVER (0x00E63586)
 * ============================================================================
 */
void REM_FILE_$SERVER(void)
{
    rem_file_server_frame_t f;
    boolean   holds_lock;           /* D2 */
    boolean   holds_netbuf;         /* D3 */
    uint16_t  extra_len;            /* D4 on entry, bulk length later */
    uint32_t  peer_node;            /* D5 */
    void     *bulk = NULL;          /* A4 */
    uint8_t   opcode;

    ML_$EXCLUSION_START(&REM_FILE_$SOCK_LOCK);      /* 0x00E63594 */
    holds_lock   = -1;                              /* 0x00E635A2 st D2b */
    holds_netbuf = 0;                               /* 0x00E635A4 clr.b D3b */

    APP_$RECEIVE(REM_FILE_SERVER_SOCK, &f.rcv, &f.local_status);

    /* 0x00E635BE: an empty queue is reported verbatim. */
    if (f.local_status == rem_file_$rcv_queue_empty) {
        f.local_status = rem_file_$rcv_queue_empty;
        goto unwind;
    }
    if (f.local_status != status_$ok) {
        goto unwind;
    }

    /* ---- receive parse (0x00E635DA-0x00E637E2) ---- */

    /* 0x00E635DA: the arrival clock, six bytes copied as a long plus a word. */
    for (int i = 0; i < 6; i++) {
        f.clock_recv[i] = f.rcv.clock[i];
    }

    /* 0x00E635E6: bits 15..22 of the longword at rcv+0x26 become the high
     * nibble of the log byte. */
    {
        uint32_t d1 = (f.rcv.f_26 & 0x007F8000u) >> 15;

        f.log.f.log_nibble = (uint8_t)((f.log.f.log_nibble & 0x0F) |
                                       (uint8_t)(d1 << 4));
    }
    /* 0x00E63600: keep the top 12 bits, then OR in the header's node. */
    f.log.node &= 0xFFF00000u;
    f.log.node |= *(uint32_t *)(void *)((uint8_t *)f.rcv.hdr + 0x0E);

    if (NETLOG_$OK_TO_LOG_SERVER < 0) {             /* 0x00E63614 */
        TIME_$ABS_CLOCK((clock_t *)(void *)f.clock_start);
    }

    /* 0x00E63628: service backlog histogram, indexed by the depth byte at
     * +0x15 of the service-info record. */
    {
        uint16_t depth = NETWORK_$SERVICE_INFO_PTR[0x15];

        f.tmp_index = depth;
        if (depth <= 8) {
            NETWORK_$FILE_BACKLOG[depth]++;
        } else {
            NETWORK_$FILE_BACKLOG_OVERFLOW++;
        }
    }

    f.pkt_src_override = f.rcv.f_18;                /* 0x00E63654 */
    f.pkt_routing_key  = f.rcv.f_1c;                /* 0x00E6365A */

    {
        uint8_t *hdr = (uint8_t *)f.rcv.hdr;
        uint16_t len;

        extra_len         = *(uint16_t *)(hdr + 0x04);
        f.pkt_src_node    = *(uint32_t *)(hdr + 0x08);
        peer_node         = *(uint32_t *)(hdr + 0x0E);
        f.pkt_dest_sock   = *(uint16_t *)(hdr + 0x12);
        f.pkt_request_id  = *(uint16_t *)(hdr + 0x06);

        len = *(uint16_t *)(hdr + 0x02);            /* 0x00E6367E */
        if (len > REM_FILE_REQUEST_MAX) {
            len = REM_FILE_REQUEST_MAX;
        }
        f.request_len = len;
    }

    OS_$DATA_COPY(f.rcv.data, &f.request,
                  (uint32_t)f.request_len);         /* 0x00E63690 */

    /* 0x00E636A8: only the LOW word is masked, so the header address keeps
     * its top 16 bits. */
    {
        uint32_t hdr_addr = (uint32_t)(uintptr_t)f.rcv.data;

        f.netbuf_hdr_addr = (hdr_addr & 0xFFFF0000u) | (hdr_addr & 0x0000FC00u);
    }
    NETBUF_$RTN_HDR(&f.netbuf_hdr_addr);

    /* 0x00E636C0: four longwords of buffer descriptor. */
    for (int i = 0; i < 4; i++) {
        f.netbuf_hdr[i] = f.rcv.bufs[i];
    }

    if (f.netbuf_hdr[0] != 0) {
        opcode = f.request.opcode;

        if ((opcode == SERVER_OP_DIR_GET_ENTRY) || (opcode == SERVER_OP_DIR_LIST)) {
            if (extra_len > REM_FILE_BULK_MAX) {
                goto request_too_long;              /* 0x00E636EE */
            }
            NETBUF_$GETVA(f.netbuf_hdr[0], &f.netbuf_va, &f.local_status);
            bulk = (void *)(uintptr_t)f.netbuf_va;
            if (f.local_status != status_$ok) {
                CRASH_SYSTEM(&f.local_status);      /* 0x00E63710 */
            }
            if (f.request.opcode == SERVER_OP_DIR_GET_ENTRY) {
                REQ_L(&f, -0x3A6) = (uint32_t)(uintptr_t)bulk;
            } else {
                REQ_L(&f, -0x3AA) = (uint32_t)(uintptr_t)bulk;
                REQ_L(&f, -0x38C) = (uint32_t)(uintptr_t)bulk;
            }
            holds_netbuf = holds_lock;              /* 0x00E63796 */
        } else if (opcode == REM_FILE_OP_ACL_CREATE) {
            if (extra_len > REM_FILE_BULK_MAX) {
                goto request_too_long;              /* 0x00E63744 */
            }
            NETBUF_$GETVA(f.netbuf_hdr[0], &f.netbuf_va, &f.local_status);
            bulk = (void *)(uintptr_t)f.netbuf_va;
            if (f.local_status != status_$ok) {
                CRASH_SYSTEM(&f.local_status);
            }
            REQ_L(&f, -0x3FC) = (uint32_t)(uintptr_t)bulk;
            holds_netbuf = holds_lock;
        } else {
            /* 0x00E6379A: append what fits onto the request body, then
             * release the whole buffer chain. */
            uint8_t *tail = (uint8_t *)&f.request + f.request_len;
            uint32_t room = (uint32_t)REM_FILE_REQUEST_MAX - f.request_len;
            uint32_t take = (uint32_t)extra_len;

            if (take > room) {
                take = room;
            }
            PKT_$DAT_COPY(f.netbuf_hdr, (int16_t)take, (char *)tail);
            PKT_$DUMP_DATA(f.netbuf_hdr, (int16_t)extra_len);
        }
    }

    goto build_response_header;

request_too_long:
    PKT_$DUMP_DATA(f.netbuf_hdr, (int16_t)extra_len);   /* 0x00E6374A */
    f.local_status = rem_file_$request_too_long;
    goto unwind;

build_response_header:
    /* 0x00E637E4 */
    f.response.magic    = REM_FILE_RESPONSE_MAGIC;
    f.response.opcode   = (uint8_t)(f.request.opcode + 1);
    f.response.pkt_flag = 1;

    if ((NETWORK_$CAPABLE_FLAGS & NETWORK_CAP_FILE_SERVER) == 0) {
        f.response.status = rem_file_$service_not_enabled;
        goto reply_len_default;                     /* 0x00E64144 */
    }

    /* 0x00E63810: an unknown protocol version is answered as opcode 2. */
    if (f.request.version != 1) {
        f.request.opcode = 2;
    }

    /* 0x00E6381E: everything except the node-crash opcode runs unlocked. */
    if (f.request.opcode != REM_FILE_OP_UNLOCK_ALL) {
        ML_$EXCLUSION_STOP(&REM_FILE_$SOCK_LOCK);
        holds_lock = 0;
    }

    opcode = f.request.opcode;

    /* ---- DIR_$SERVER delegation (0x00E6383A-0x00E63954) ---- */
    if ((opcode >= REM_FILE_DIR_OP_FIRST) && (opcode <= REM_FILE_DIR_OP_LAST)) {
        if ((holds_netbuf >= 0) && (opcode == SERVER_OP_DIR_LIST)) {
            REQ_L(&f, -0x3AA) = (uint32_t)(uintptr_t)REQ_P(&f, -0x388);
            NETBUF_$GET_DAT(f.netbuf_hdr);
            NETBUF_$GETVA(f.netbuf_hdr[0], &f.netbuf_va, &f.local_status);
            bulk = (void *)(uintptr_t)f.netbuf_va;
            /* 0x00E6388E: only the LOW word of the status is tested here. */
            if ((uint16_t)(f.local_status & 0xFFFF) != 0) {
                CRASH_SYSTEM(&f.local_status);
            }
            REQ_L(&f, -0x38C) = (uint32_t)(uintptr_t)bulk;
            holds_netbuf = -1;
        } else if ((opcode == SERVER_OP_DIR_READ_DIR) ||
                   (opcode == SERVER_OP_DIR_READ_LINK)) {
            NETBUF_$GET_DAT(f.netbuf_hdr);
            NETBUF_$GETVA(f.netbuf_hdr[0], &f.netbuf_va, &f.local_status);
            bulk = (void *)(uintptr_t)f.netbuf_va;
            if ((uint16_t)(f.local_status & 0xFFFF) != 0) {
                CRASH_SYSTEM(&f.local_status);
            }
            if (f.request.opcode == SERVER_OP_DIR_READ_LINK) {
                REQ_L(&f, -0x3A6) = (uint32_t)(uintptr_t)bulk;
            } else {
                REQ_L(&f, -0x39E) = (uint32_t)(uintptr_t)bulk;
            }
            holds_netbuf = -1;
        } else if ((holds_netbuf < 0) && (opcode == SERVER_OP_DIR_GET_ENTRY)) {
            /* 0x00E6391A: an offset relative to the frame itself. */
            REQ_L(&f, -0x3A6) = (uint32_t)(uintptr_t)
                ((uint8_t *)&f + 0x4E4 + (int16_t)REQ_W(&f, -0x3AA) - 0x3A2);
        }

        DIR_$SERVER(&f.request, &f.response, &f.reply_len);

        if ((f.request.opcode == SERVER_OP_DIR_GET_ENTRY) && (holds_netbuf < 0)) {
            goto release_netbuf;                    /* 0x00E639DC */
        }
        goto send_reply;
    }

    /* ---- ACL_$SERVER delegation (0x00E63958-0x00E639F8) ---- */
    if ((opcode >= REM_FILE_ACL_OP_FIRST) && (opcode <= REM_FILE_ACL_OP_LAST)) {
        if (opcode == REM_FILE_OP_ACL_IMAGE) {
            NETBUF_$GET_DAT(f.netbuf_hdr);
            NETBUF_$GETVA(f.netbuf_hdr[0], &f.netbuf_va, &f.local_status);
            bulk = (void *)(uintptr_t)f.netbuf_va;
            if ((uint16_t)(f.local_status & 0xFFFF) != 0) {
                CRASH_SYSTEM(&f.local_status);
            }
            REQ_L(&f, -0x428) = (uint32_t)(uintptr_t)bulk;
            holds_netbuf = -1;
        }

        ACL_$SERVER(&f.request, &f.response, &f.reply_len);

        if (f.request.opcode != REM_FILE_OP_ACL_CREATE) {
            goto send_reply;
        }

release_netbuf:                                     /* 0x00E639DC */
        f.netbuf_va = (uint32_t)(uintptr_t)bulk;
        NETBUF_$RTN_DAT(NETBUF_$RTNVA(&f.netbuf_va));
        holds_netbuf = 0;
        goto send_reply;
    }

    /* ---- inline opcode dispatch (0x00E639FC) ---- */
    switch (opcode) {

    case REM_FILE_OP_TEST:                            /* 0x00E63E40 */
        f.response.status = status_$ok;
        goto reply_len_default;

    case REM_FILE_OP_SET_ATTRIBUTE:                   /* 0x00E63AD4 */
        server_set_attribute(&f);
        break;

    case REM_FILE_OP_TRUNCATE:                        /* 0x00E63B00 */
        server_truncate_delete(&f);
        break;

    case REM_FILE_OP_LOCK:
    case REM_FILE_OP_LOCK_EXT: {                 /* 0x00E63B08 */
        if (opcode == REM_FILE_OP_LOCK_EXT) {
            f.lock_flags = (uint16_t)(2 | REQ_W(&f, -0x420));
            /* 0x00E63B30: the ACL context is the address of a cell holding
             * the address of the request's SID block. */
            f.lock_acl_cell = REQ_P(&f, -0x3FC);
            FILE_$PRIV_LOCK(&f.request.uid, 0,
                            REQ_W(&f, -0x422),      /* side */
                            REQ_W(&f, -0x424),      /* lock_mode */
                            -1,                     /* local_only */
                            f.lock_flags,
                            f.pkt_request_id,       /* key */
                            REQ_L(&f, -0x42C),      /* rem_key */
                            REQ_L(&f, -0x428),      /* rem_node */
                            f.pkt_routing_key,      /* rem_extra */
                            &f.lock_acl_cell,
                            REQ_W(&f, -0x398),      /* rem_wait */
                            (uint32_t *)&f.lock_slot,
                            (uint16_t *)RSP_P(&f, -0x0E4),
                            &f.response.status);
            f.reply_len = 0xBE;
        } else {
            f.lock_flags = 0x8A;                    /* 0x00E63B70 */
            FILE_$PRIV_LOCK(&f.request.uid, 0,
                            REQ_W(&f, -0x422),
                            REQ_W(&f, -0x424),
                            -1,
                            f.lock_flags,
                            f.pkt_request_id,
                            REQ_L(&f, -0x42C),
                            REQ_L(&f, -0x428),
                            f.pkt_routing_key,
                            (void **)&REM_FILE_$NIL_CONST,
                            REQ_W(&f, -0x398),
                            (uint32_t *)&f.lock_slot,
                            (uint16_t *)RSP_P(&f, -0x0E4),
                            &f.response.status);
            f.reply_len = 0x10;
        }

        /* 0x00E63BC0: the local table being full is reported as a distinct
         * remote status. */
        if (f.response.status == file_$local_lock_table_full) {
            f.response.status = 0x000F000A;
            goto send_reply;
        }
        if (f.response.status != status_$ok) {
            goto send_reply;
        }

        if (f.request.opcode == REM_FILE_OP_LOCK_EXT) {
            file_$obj_loc_t *desc = (file_$obj_loc_t *)RSP_P(&f, -0x104);

            /* 0x00E63BEA: eight longwords of location descriptor. */
            for (int i = 0; i < 8; i++) {
                ((uint32_t *)desc)[i] = ((uint32_t *)REQ_P(&f, -0x41C))[i];
            }
            if (((desc->uid.high >> 24) & 0xFF) == 0) {
                RSP_L(&f, -0x190) = desc->uid.high;
                RSP_L(&f, -0x18C) = desc->uid.low;
            } else {
                AST_$GET_ATTRIBUTES(desc, 0x81, RSP_P(&f, -0x194),
                                    &f.response.status);
            }
            f.reply_len = 0xBE;
            RSP_W(&f, -0x198) = 1;
        } else {
            f.zero_long = 0;                        /* 0x00E63C3E */
            AST_$GET_DTV(&f.request.uid, f.zero_long,
                         (uint32_t *)RSP_P(&f, -0x198), &f.local_status);
            if (f.request_len == 0x1E) {
                RSP_W(&f, -0x194) =
                    (uint16_t)((RSP_W(&f, -0x194) << 3) & 0x00F8);
                f.reply_len = 0x0E;
            } else {
                f.reply_len = 0x10;
                RSP_W(&f, -0x192) = 1;
            }
        }
        break;
    }

    case REM_FILE_OP_UNLOCK: {                        /* 0x00E63C8C */
        if (((int8_t)REQ_B(&f, -0x418) < 0) && (f.request_len >= 0x22)) {
            file_$obj_loc_t *desc = (file_$obj_loc_t *)RSP_P(&f, -0x104);

            desc->uid = f.request.uid;
            desc->flags &= (int8_t)~FILE_OBJ_LOC_SCRATCH;
            AST_$GET_ATTRIBUTES(desc, 0x280, RSP_P(&f, -0x194),
                                &f.local_status);
            RSP_L(&f, -0x190) = RSP_L(&f, -0x170);
            RSP_W(&f, -0x18C) = RSP_W(&f, -0x16C);
            RSP_B(&f, -0x191) = (f.local_status == status_$ok) ? 0xFF : 0x00;
        }

        /*
         * 0x00E63CDE-0x00E63D00 pushes ten arguments (32 bytes), right to
         * left:
         *   pea (-0x19c,A6)     status_ret = &f.response.status
         *   pea (-0x198,A6)     dtv_out
         *   move.l (-0x428,A6)  rem_node
         *   move.l (-0x42c,A6)  rem_key
         *   move.w (-0x41c,A6)  key
         *   st                  by_key = TRUE
         *   clr.w               asid = 0
         *   move.w (-0x424,A6)  lock_mode
         *   clr.l               lock_slot = 0
         *   pea (-0x434,A6)     file_uid = &f.request.uid
         */
        RSP_B(&f, -0x192) =
            (uint8_t)FILE_$PRIV_UNLOCK(&f.request.uid,
                                       0,                       /* lock_slot */
                                       REQ_W(&f, -0x424),       /* lock_mode */
                                       0,                       /* asid      */
                                       -1,                      /* by_key    */
                                       REQ_W(&f, -0x41C),       /* key       */
                                       REQ_L(&f, -0x42C),       /* rem_key   */
                                       REQ_L(&f, -0x428),       /* rem_node  */
                                       (uint32_t *)RSP_P(&f, -0x198),
                                       &f.response.status);
        f.reply_len = 0x16;
        break;
    }

    case REM_FILE_OP_NEIGHBORS:                       /* 0x00E63ADC */
        RSP_B(&f, -0x198) = (uint8_t)FILE_$NEIGHBORS(&f.request.uid,
                                                     (uid_t *)REQ_P(&f, -0x42C),
                                                     &f.response.status);
        f.reply_len = 0x0A;
        break;

    case REM_FILE_OP_UNLOCK_ALL:                      /* 0x00E63D18 */
        if ((NETWORK_$DISKLESS < 0) && (peer_node == NETWORK_$MOTHER_NODE)) {
            CRASH_SHOW_STRING(REM_FILE_$DISKLESS_CRASH_MSG);
            CRASH_SYSTEM(&REM_FILE_$COMMS_PROBLEM_STATUS);
        }

        f.lock_scan_index = 0;                      /* 0x00E63D42 */
        for (;;) {
            FILE_$READ_LOCK_ENTRYI(&UID_$NIL, &f.lock_scan_index,
                                   &f.lock_info, &f.local_status);
            if (f.local_status != status_$ok) {
                break;
            }
            if ((f.lock_info.owner_node & 0x000FFFFFu) != peer_node) {
                continue;
            }
            /*
             * 0x00E63D7A-0x00E63D98, right to left:
             *   pea (-0x494,A6)     status_ret = &f.local_status
             *   pea (-0x46c,A6)     dtv_out
             *   move.l (-0x454,A6)  rem_node = f.lock_info.owner_node (+0x0C)
             *   move.l (-0x458,A6)  rem_key  = f.lock_info.context    (+0x08)
             *   move.w (-0x44c,A6)  key      = f.lock_info.sequence   (+0x14)
             *   st                  by_key   = TRUE
             *   clr.l               lock_mode = 0, asid = 0
             *   clr.l               lock_slot = 0
             *   pea (-0x460,A6)     file_uid = &f.lock_info (its UID is at +0)
             */
            (void)FILE_$PRIV_UNLOCK((uid_t *)&f.lock_info,
                                    0,                      /* lock_slot */
                                    0,                      /* lock_mode */
                                    0,                      /* asid      */
                                    -1,                     /* by_key    */
                                    f.lock_info.sequence,   /* key       */
                                    f.lock_info.context,    /* rem_key   */
                                    f.lock_info.owner_node, /* rem_node  */
                                    (uint32_t *)f.scratch_070,
                                    &f.local_status);
        }

        AREA_$FREE_FROM(peer_node);                 /* 0x00E63DA4 */
        if (NETWORK_$REALLY_DISKLESS < 0) {
            goto unwind;                            /* 0x00E63DB4 */
        }
        f.crash_node = peer_node;
        DIR_$DROP_MOUNT(&NAME_$NODE_UID, &UID_$NIL, &f.crash_node,
                        &f.local_status);
        goto unwind;                                /* no reply is sent */

    case REM_FILE_OP_PURIFY:                          /* 0x00E63E16 */
        (void)AST_$PURIFY(&f.request.uid,
                          (uint16_t)(4 | REQ_W(&f, -0x42C)),
                          (int16_t)REQ_W(&f, -0x42A),
                          &REM_FILE_$NIL_CONST, 0, &f.response.status);
        goto reply_len_default;

    case REM_FILE_OP_LOCAL_READ_LOCK:                 /* 0x00E63DDA */
        FILE_$LOCAL_READ_LOCK(&f.request.uid,
                              (file_lock_info_internal_t *)RSP_P(&f, -0x198),
                              &f.response.status);
        f.reply_len = 0x2A;
        break;

    case REM_FILE_OP_SET_DEF_ACL:                     /* 0x00E63E48 */
        if (REQ_W(&f, -0x41C) != 3) {
            f.response.status = file_$bad_reply_received_from_remote_node;
        } else {
            boolean super = (boolean)REQ_B(&f, -0x41A);

            if (super < 0) {
                ACL_$ENTER_SUPER();
            }
            DIR_$OLD_SET_DEFAULT_ACL(&f.request.uid,
                                     (uid_t *)REQ_P(&f, -0x42C),
                                     (uid_t *)REQ_P(&f, -0x424),
                                     &f.response.status);
            if (super < 0) {
                ACL_$EXIT_SUPER();
            }
        }
        goto stale_entry_check;                     /* 0x00E63E8C */

    case REM_FILE_OP_LOCAL_VERIFY:               /* 0x00E63E02 */
        FILE_$LOCAL_LOCK_VERIFY((lock_verify_request_t *)REQ_P(&f, -0x42C),
                                &f.response.status);
        goto reply_len_default;

    case REM_FILE_OP_NAME_GET_ENTRYU:                       /* 0x00E63DFA */
        server_get_entry_sids(&f);
        break;

    case REM_FILE_OP_GET_SEG_MAP:                     /* 0x00E63EA4 */
        f.zero_long = 0;
        if (f.request_len == 0x14) {
            f.seg_map_flag = ((int8_t)REQ_B(&f, -0x426) < 0) ? 1 : 0;
            AST_$GET_SEG_MAP(&f.request.uid,
                             (uint32_t)REQ_W(&f, -0x42C) << 15,
                             f.zero_long, 1, 0x20,
                             f.seg_map_flag,
                             (uint32_t *)RSP_P(&f, -0x198),
                             &f.response.status);
        } else {
            /* 0x00E63EEC: f.seg_map_flag is deliberately not re-set here. */
            AST_$GET_SEG_MAP(&f.request.uid,
                             REQ_L(&f, -0x424) << 10,
                             f.zero_long,
                             (uint32_t)REQ_W(&f, -0x420),
                             (uint32_t)REQ_W(&f, -0x41E),
                             f.seg_map_flag,
                             (uint32_t *)RSP_P(&f, -0x198),
                             &f.response.status);
        }
        f.reply_len = 0x28;
        break;

    case REM_FILE_OP_INVALIDATE:                      /* 0x00E63F30 */
        AST_$INVALIDATE(&f.request.uid, REQ_L(&f, -0x42C), REQ_L(&f, -0x428),
                        (int16_t)REQ_B(&f, -0x424), &f.response.status);
        goto reply_len_default;

    case REM_FILE_OP_RESERVE:                         /* 0x00E63F50 */
        AST_$RESERVE(&f.request.uid, REQ_L(&f, -0x42C), REQ_L(&f, -0x428),
                     &f.response.status);
        goto reply_len_default;

    case REM_FILE_OP_NAME_ADD_HARD_LINKU:                   /* 0x00E63F6E */
        server_unmap_name(&f, (char *)REQ_P(&f, -0x42C),
                          (int16_t *)REQ_P(&f, -0x40C));
        if ((int8_t)REQ_B(&f, -0x400) < 0) {
            ACL_$ENTER_SUPER();
        }
        DIR_$OLD_ADD_HARD_LINKU(&f.request.uid, (char *)REQ_P(&f, -0x42C),
                                (uint16_t *)REQ_P(&f, -0x40C),
                                (uid_t *)REQ_P(&f, -0x40A), &f.dir_status);
        f.response.status = f.dir_status;
        if ((int8_t)REQ_B(&f, -0x400) < 0) {
            ACL_$EXIT_SUPER();
        }
        if (f.dir_status != status_$naming_directory_locked) {
            goto reply_len_default;
        }
        f.response.pkt_flag = 0xFFFF;
        REM_FILE_$STALE_LINK_COUNT++;
        goto reply_len_default;

    case REM_FILE_OP_GENERATE_UID:                    /* 0x00E63FCE */
        server_generate_uid(&f);
        break;

    case REM_FILE_OP_DROP_HARD_LINKU:                  /* 0x00E63FC6 */
        server_drop_link(&f);
        break;

    case REM_FILE_OP_CREATE_TYPE_PRESR10:                  /* 0x00E63FD6 */
        f.create_flags = 2;
        if (REQ_W(&f, -0x426) == 0) {
            f.create_flags = 3;
        }
        (void)FILE_$PRIV_CREATE((int16_t)REQ_W(&f, -0x42C), &UID_$NIL,
                                &f.request.uid, (uid_t *)REQ_P(&f, -0x424),
                                0, f.create_flags, NULL, &f.response.status);
        f.reply_len = 0x12;
        RSP_W(&f, -0x190) = REQ_W(&f, -0x426);
        break;

    case REM_FILE_OP_CREATE_TYPE:                     /* 0x00E64020 */
        (void)FILE_$PRIV_CREATE((int16_t)REQ_W(&f, -0x3E0),
                                (const uid_t *)REQ_P(&f, -0x424),
                                (uid_t *)REQ_P(&f, -0x41C),
                                (uid_t *)REQ_P(&f, -0x42C),
                                REQ_L(&f, -0x3E4),
                                (uint16_t)(2 | REQ_W(&f, -0x3DE)),
                                (uid_t *)REQ_P(&f, -0x414),
                                &f.response.status);
        if ((f.response.status == status_$ok) ||
            (f.response.status == 0x00020007)) {
            file_$obj_loc_t *desc = (file_$obj_loc_t *)RSP_P(&f, -0x104);

            desc->uid.high = REQ_L(&f, -0x42C);
            desc->uid.low  = REQ_L(&f, -0x428);
            desc->flags   &= (int8_t)~FILE_OBJ_LOC_SCRATCH;
            AST_$GET_ATTRIBUTES(desc, 1, RSP_P(&f, -0x194),
                                &f.local_status);
            if (f.local_status != status_$ok) {
                f.response.status = f.local_status;
            }
        }
        f.reply_len = 0xBE;
        break;

    case REM_FILE_OP_FILE_SET_PROT:                        /* 0x00E640A0 */
    case REM_FILE_OP_FILE_SET_ATTRIB:
        server_set_prot_attrib(&f);
        break;

    case REM_FILE_OP_CREATE_AREA:                     /* 0x00E640A8 */
        RSP_W(&f, -0x198) =
            AREA_$CREATE_FROM(peer_node, REQ_L(&f, -0x42C),
                              (f.request_len < 0x1C) ? REQ_L(&f, -0x42C)
                                                     : REQ_L(&f, -0x420),
                              (int32_t)REQ_L(&f, -0x428),
                              &f.response.status);
        RSP_W(&f, -0x196) = REM_FILE_BULK_MAX;
        f.reply_len = 0x0C;
        break;

    case REM_FILE_OP_DELETE_AREA:                     /* 0x00E640EC */
        AREA_$DELETE_FROM(REQ_W(&f, -0x424), peer_node, REQ_L(&f, -0x428),
                          &f.response.status);
        goto reply_len_default;

    case REM_FILE_OP_GROW_AREA:                       /* 0x00E64106 */
        AREA_$GROW_TO(REQ_W(&f, -0x424), REQ_L(&f, -0x42C),
                      (f.request_len < 0x1C) ? REQ_L(&f, -0x42C)
                                             : REQ_L(&f, -0x420),
                      &f.response.status);
        goto reply_len_default;

    default:                                        /* 0x00E64136 */
        f.response.opcode = 0x03;
        f.response.status = file_$bad_reply_received_from_remote_node;
        goto reply_len_default;
    }
    goto send_reply;

stale_entry_check:                                  /* 0x00E63E8C */
    if (f.response.status == status_$naming_directory_locked) {
        f.response.pkt_flag = 0xFFFF;
        REM_FILE_$STALE_LINK_COUNT++;
    }
    /* fall through */

reply_len_default:                                  /* 0x00E64144 */
    f.reply_len = 8;
    /* fall through */

send_reply:                                         /* 0x00E6414A */
    if (NETLOG_$OK_TO_LOG_SERVER < 0) {
        TIME_$ABS_CLOCK((clock_t *)(void *)f.clock_end);
    } else {
        /* 0x00E64160: only the first longword of the clock is written. */
        *(uint32_t *)(void *)f.clock_end = TIME_$CLOCKH;
    }

    /* 0x00E64168: anything past 0x200 goes out as bulk data taken from the
     * caller's frame at A6+0x60. */
    if (f.reply_len > REM_FILE_REPLY_SPLIT) {
        f.reply_hdr_len = REM_FILE_REPLY_SPLIT;
        extra_len = (uint16_t)(f.reply_len - REM_FILE_REPLY_SPLIT);
        /* 0x00E6417C `lea (0x60,A6),A4`: reply + 0x200, which runs off the
         * end of this frame into the caller's (see REM_FILE_SPILL_BYTES).
         * Overwriting A4 here also loses the netbuf address the DIR/ACL paths
         * put in it, so the release at 0x00E6425C frees this pointer instead
         * - the original does exactly that. */
        bulk = RSP_P(&f, 0x60);
    } else {
        f.reply_hdr_len = f.reply_len;              /* 0x00E64184 */

        if ((f.request.opcode == SERVER_OP_DIR_LIST) &&
            (f.response.status == status_$ok)) {
            extra_len = RSP_W(&f, -0x172);
        } else if ((f.request.opcode == SERVER_OP_DIR_READ_DIR) &&
                   (f.response.status == status_$ok)) {
            extra_len = (uint16_t)RSP_L(&f, -0x184);
        } else if ((f.request.opcode == SERVER_OP_DIR_READ_LINK) &&
                   ((f.response.status == status_$ok) ||
                    (f.response.status == 0x000E002C))) {
            extra_len = RSP_W(&f, -0x18C);
        } else if ((f.request.opcode == REM_FILE_OP_ACL_IMAGE) &&
                   (f.response.status == status_$ok)) {
            extra_len = (REQ_W(&f, -0x42C) == 4) ? 0x3FC : 0x400;
        } else {
            extra_len = 0;                          /* 0x00E64202 */
        }
    }

    if (extra_len > REM_FILE_BULK_MAX) {            /* 0x00E64204 */
        f.local_status = rem_file_$reply_too_long;
    }

    if (f.local_status == status_$ok) {
        PKT_$SEND_INTERNET(f.pkt_routing_key, peer_node, f.pkt_dest_sock,
                           (int32_t)f.pkt_src_override, f.pkt_src_node,
                           REM_FILE_SERVER_SOCK,
                           REM_FILE_$SERVER_PKT_INFO, f.pkt_request_id,
                           &f.response, f.reply_hdr_len,
                           bulk, (int16_t)extra_len,
                           &f.send_len_out, &f.send_extra_out,
                           &f.local_status);
    }

    if (holds_netbuf < 0) {                         /* 0x00E6425C */
        f.netbuf_va = (uint32_t)(uintptr_t)bulk;
        NETBUF_$RTN_DAT(NETBUF_$RTNVA(&f.netbuf_va));
        holds_netbuf = 0;
    }

    if (f.local_status != status_$ok) {
        /* 0x00E6452A: a reply that could not be sent leaves the object the
         * requester was growing truncated again. */
        if (f.response.status != status_$ok) {
            goto unwind;
        }
        if (f.request.opcode != 0x06) {
            goto unwind;
        }
        AST_$TRUNCATE((uid_t *)RSP_P(&f, -0x198), 0, 1, f.trunc_result,
                      &f.local_status);
        goto unwind;
    }

    if (NETLOG_$OK_TO_LOG_SERVER >= 0) {
        goto unwind;
    }

    /* 0x00E6428E */
    f.log.f.log_op = server_log_code(f.request.opcode);

    NETLOG_$LOG_IT(REM_FILE_LOG_KIND, (uint32_t *)&f.request.uid, 0, 0,
                   f.log.w[0], f.log.f.log_low,
                   (uint16_t)((int32_t)(CLOCK_LO(f.clock_start) -
                                        CLOCK_LO(f.clock_recv)) /
                              REM_FILE_LOG_TICKS_PER_UNIT),
                   (uint16_t)((int32_t)(CLOCK_LO(f.clock_end) -
                                        CLOCK_LO(f.clock_start)) /
                              REM_FILE_LOG_TICKS_PER_UNIT));

unwind:                                             /* 0x00E6455A */
    if (holds_netbuf < 0) {
        f.netbuf_va = (uint32_t)(uintptr_t)bulk;
        NETBUF_$RTN_DAT(NETBUF_$RTNVA(&f.netbuf_va));
    }
    if (holds_lock < 0) {
        ML_$EXCLUSION_STOP(&REM_FILE_$SOCK_LOCK);
    }
}
