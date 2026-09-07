/*
 * REM_FILE_$UNLOCK - release a lock held on a file that lives on another node
 *
 * Original address: 0x00E61D1C, 260 bytes.  Re-emitted from the disassembly
 * for bead source-zm7e; every basic block of the original is accounted for
 * below.
 *
 * Frame (`link.w A6,-0x180` at 0x00E61D1C):
 *
 *   A6-0x17C  uint16_t  received_len   output of REM_FILE_$SEND_REQUEST
 *   A6-0x17A  uint16_t  packet_id      output of REM_FILE_$SEND_REQUEST
 *   A6-0x178  int16_t   nil_word       cleared at 0x00E61D8C and passed three
 *                                      times (extra_data, bulk_data, bulk_len)
 *   A6-0x174  status_$t dts_status     AST_$SET_DTS' status_ret
 *   A6-0x170  request   0x22 bytes     (rem_file_unlock_req_t)
 *   A6-0x0C0  response  0xBE bytes     (rem_file_unlock_resp_t + slack)
 *
 * A5 is the REM_FILE module base (`lea (0xe823fc).l,A5` at 0x00E61D24); this
 * function touches no A5 global, it only has to establish it for the callee.
 *
 * Callers (both confirmed against the pushes):
 *   0x00E60298  FILE_$PRIV_UNLOCK    - remote_node comes from NODE_$ME
 *   0x00E607F8  FILE_$VERIFY_LOCK_HOLDER
 */

#include "rem_file/rem_file_internal.h"

/*
 * ----------------------------------------------------------------------------
 * Wire records
 * ----------------------------------------------------------------------------
 */

/* `move.w #0x22,-(SP)` at 0x00E61DB4 - the request length handed to
 * REM_FILE_$SEND_REQUEST, and the length REM_FILE_$SERVER checks before it
 * looks at the release flag (`cmpi.w #0x22` in the 0x0C case). */
#define REM_FILE_UNLOCK_REQ_LEN     0x22

/* `move.w #0xbe,-(SP)` at 0x00E61DA6 - the response buffer size. */
#define REM_FILE_UNLOCK_RESP_SIZE   0xBE

/* `move.b #-0x80,(-0x16e,A6)` at 0x00E61D42 / `move.b #0xc,(-0x16d,A6)` at
 * 0x00E61D48.  REM_FILE_$SERVER dispatches this as REM_FILE_OP_UNLOCK (0x0C)
 * at 0x00E63A2A; both constants now come from rem_file/rem_file_internal.h
 * (bead source-8joj). */
#define REM_FILE_UNLOCK_MAGIC       REM_FILE_REQ_MAGIC
#define REM_FILE_UNLOCK_OPCODE      REM_FILE_OP_UNLOCK

/* `move.w #0x3,(-0x158,A6)` at 0x00E61D6A.  Every REM_FILE request carries the
 * same {version word == 3, super-mode boolean} pair right after its payload
 * (REM_FILE_$PURIFY at 0x00E62290/0x00E622AA puts it at +0x10/+0x12,
 * REM_FILE_$SET_DEF_ACL at 0x00E6232A at +0x1C/+0x1E, and the server reads the
 * pair for opcode 0x18 at request+0x1C/+0x1E). */
#define REM_FILE_REQ_VERSION        3

/*
 * The unlock request, 0x22 bytes, built at A6-0x170.
 *
 * Two words and two odd bytes inside the record are never written by
 * REM_FILE_$UNLOCK - they go out holding whatever the stack held.  They are
 * named `uninit_*` rather than `pad_*` to record that the original leaks
 * stack, and the C leaves them uninitialised for the same reason.
 */
typedef struct rem_file_unlock_req_t {
    uint16_t    msg_type;       /* 0x00: filled in by REM_FILE_$SEND_REQUEST */
    uint8_t     magic;          /* 0x02: 0x80        (0x00E61D42) */
    uint8_t     opcode;         /* 0x03: 0x0C        (0x00E61D48) */
    uid_t       file_uid;       /* 0x04: obj_loc->uid (0x00E61D4E-0x00E61D56) */
    uint32_t    rem_key;        /* 0x0C: arg rem_key     (0x00E61D5A) */
    uint32_t    rem_node;       /* 0x10: arg rem_node    (0x00E61D5E) */
    uint16_t    lock_mode;      /* 0x14: arg unlock_mode (0x00E61D66) */
    uint16_t    uninit_16;      /* 0x16: never written */
    uint16_t    version;        /* 0x18: 3           (0x00E61D6A) */
    boolean     super_user;     /* 0x1A: ACL_$SUPER_COUNT[cur] > 0
                                 *       (0x00E61D70-0x00E61D84) */
    uint8_t     uninit_1b;      /* 0x1B: never written */
    uint16_t    lock_key;       /* 0x1C: arg lock_key (0x00E61D62); the server
                                 *       passes it to FILE_$PRIV_UNLOCK as
                                 *       `key` */
    uint16_t    uninit_1e;      /* 0x1E: never written */
    boolean     release;        /* 0x20: arg release_flag (0x00E61D88) */
    uint8_t     uninit_21;      /* 0x21: never written */
} rem_file_unlock_req_t;

/* Every field is naturally aligned at its image offset, so no packing is
 * needed and the offsets hold on a host build too.  Only the trailing
 * padding differs (m68k aligns longs to 2, a 64-bit host to 4), which is why
 * the size check is m68k-only - the wire length is the literal 0x22 the image
 * pushes, never `sizeof`. */
_Static_assert(offsetof(rem_file_unlock_req_t, magic)      == 0x02, "unlock req magic");
_Static_assert(offsetof(rem_file_unlock_req_t, opcode)     == 0x03, "unlock req opcode");
_Static_assert(offsetof(rem_file_unlock_req_t, file_uid)   == 0x04, "unlock req uid");
_Static_assert(offsetof(rem_file_unlock_req_t, rem_key)    == 0x0C, "unlock req rem_key");
_Static_assert(offsetof(rem_file_unlock_req_t, rem_node)   == 0x10, "unlock req rem_node");
_Static_assert(offsetof(rem_file_unlock_req_t, lock_mode)  == 0x14, "unlock req lock_mode");
_Static_assert(offsetof(rem_file_unlock_req_t, version)    == 0x18, "unlock req version");
_Static_assert(offsetof(rem_file_unlock_req_t, super_user) == 0x1A, "unlock req super");
_Static_assert(offsetof(rem_file_unlock_req_t, lock_key)   == 0x1C, "unlock req lock_key");
_Static_assert(offsetof(rem_file_unlock_req_t, release)    == 0x20, "unlock req release");
#if defined(ARCH_M68K)
_Static_assert(sizeof(rem_file_unlock_req_t) == REM_FILE_UNLOCK_REQ_LEN,
               "sizeof unlock request");
#endif

/*
 * The unlock reply, based at A6-0x0C0 (offset 0 of the response buffer).
 * REM_FILE_$SERVER's 0x0C case (rem_file/server.c, 0x00E63C8C) writes exactly
 * these fields and sets reply_len = 0x16, which is the `cmpi.w #0x16` bound
 * tested at 0x00E61DE6.
 */
typedef struct rem_file_unlock_resp_t {
    uint16_t    msg_type;       /* 0x00 */
    uint8_t     magic;          /* 0x02 */
    uint8_t     opcode;         /* 0x03 */
    status_$t   status;         /* 0x04: extracted by REM_FILE_$SEND_REQUEST */
    /* 0x08: FILE_$PRIV_UNLOCK's dtv_out, a 48-bit Domain clock.  Split into
     * its two halves rather than embedding `clock_t`, because clock_t is six
     * bytes on the m68k and eight on a 64-bit host, which would move every
     * field after it.  `tst.l (-0xb8,A6)` at 0x00E61DD4 tests only dtv_high. */
    uint32_t    dtv_high;       /* 0x08 */
    uint16_t    dtv_low;        /* 0x0C */
    uint8_t     result;         /* 0x0E: FILE_$PRIV_UNLOCK's return byte
                                 *       (read at 0x00E61E0E) */
    boolean     attrs_valid;    /* 0x0F: server got AST_$GET_ATTRIBUTES ok
                                 *       (read at 0x00E61DE0) */
    uint32_t    dtu_high;       /* 0x10: the object's access time */
    uint16_t    dtu_low;        /* 0x14 */
    uint8_t     slack[REM_FILE_UNLOCK_RESP_SIZE - 0x16];
} rem_file_unlock_resp_t;

_Static_assert(offsetof(rem_file_unlock_resp_t, status)      == 0x04, "unlock rsp status");
_Static_assert(offsetof(rem_file_unlock_resp_t, dtv_high)    == 0x08, "unlock rsp dtv");
_Static_assert(offsetof(rem_file_unlock_resp_t, dtv_low)     == 0x0C, "unlock rsp dtv lo");
_Static_assert(offsetof(rem_file_unlock_resp_t, result)      == 0x0E, "unlock rsp result");
_Static_assert(offsetof(rem_file_unlock_resp_t, attrs_valid) == 0x0F, "unlock rsp valid");
_Static_assert(offsetof(rem_file_unlock_resp_t, dtu_high)    == 0x10, "unlock rsp dtu");
_Static_assert(offsetof(rem_file_unlock_resp_t, dtu_low)     == 0x14, "unlock rsp dtu lo");
#if defined(ARCH_M68K)
_Static_assert(sizeof(rem_file_unlock_resp_t) == REM_FILE_UNLOCK_RESP_SIZE,
               "sizeof unlock response");
#endif

/*
 * AST_$SET_DTS selector bits, from the tests in ast/set_dts.c (0x00E05540):
 *   0x02 - store `dtv` into aote+0x38
 *   0x08 - store `access_time` into aote+0x30
 */
#define AST_SET_DTS_DTV     0x0002      /* `moveq #0x2,D3`   at 0x00E61DDA */
#define AST_SET_DTS_DTU     0x0008      /* `ori.w #0x8,D3w`  at 0x00E61DEC */

/*
 * The reply must be longer than the 8-byte header before any of the payload
 * is believed (`cmpi.w #0x8,D0w; ble` at 0x00E61DCC), and at least 0x16 bytes
 * before the access time at +0x10 exists (`cmpi.w #0x16,D0w; blt` at
 * 0x00E61DE6).
 */
#define REM_FILE_UNLOCK_MIN_REPLY   0x08
#define REM_FILE_UNLOCK_DTU_REPLY   0x16

uint8_t REM_FILE_$UNLOCK(file_$obj_loc_t *location_block, uint16_t unlock_mode,
                         uint32_t rem_key, uint16_t lock_key,
                         uint32_t rem_node, boolean release_flag,
                         status_$t *status)
{
    rem_file_unlock_req_t  request;             /* A6-0x170 */
    rem_file_unlock_resp_t response;            /* A6-0x0C0 */
    uint16_t  received_len;                     /* A6-0x17C */
    uint16_t  packet_id;                        /* A6-0x17A */
    int16_t   nil_word;                         /* A6-0x178 */
    status_$t dts_status;                       /* A6-0x174 */
    uint16_t  dts_flags;                        /* D3 */

    /* 0x00E61D42-0x00E61D88: build the request. */
    request.magic     = REM_FILE_UNLOCK_MAGIC;
    request.opcode    = REM_FILE_UNLOCK_OPCODE;
    request.file_uid  = location_block->uid;    /* `lea (0x8,A2),A0` + two
                                                 * `move.l (A0)+` */
    request.rem_key   = rem_key;
    request.rem_node  = rem_node;
    request.lock_mode = unlock_mode;
    request.version   = REM_FILE_REQ_VERSION;
    /* `sgt D5b` at 0x00E61D82: 0xFF when the count is strictly positive. */
    request.super_user = REM_FILE_PROCESS_HAS_ADMIN() ? true : false;
    request.lock_key  = lock_key;
    request.release   = release_flag;

    /* 0x00E61D8C: one word local serves as the (empty) extra-data buffer, the
     * (empty) bulk buffer and the bulk-length output. */
    nil_word = 0;

    /* 0x00E61D90-0x00E61DC4, thirteen arguments pushed right to left; the
     * address info is at location_block+0x10 (`pea (0x10,A2)`). */
    REM_FILE_$SEND_REQUEST(&location_block->loc_info,
                           &request, REM_FILE_UNLOCK_REQ_LEN,
                           &nil_word, 0,
                           &response, REM_FILE_UNLOCK_RESP_SIZE,
                           &received_len,
                           &nil_word, 0, &nil_word,
                           &packet_id,
                           status);

    /* 0x00E61DC8-0x00E61DD0 / 0x00E61E14: a reply of 8 bytes or less carries
     * no payload, so the result byte is 0. */
    if ((int16_t)received_len <= REM_FILE_UNLOCK_MIN_REPLY) {
        return 0;
    }

    /* 0x00E61DD2-0x00E61DDA */
    dts_flags = 0;
    if (response.dtv_high != 0) {
        dts_flags = AST_SET_DTS_DTV;
    }

    /* 0x00E61DDC-0x00E61DEC: only when the caller asked for the object's
     * timestamps back, the server actually read them, and the reply is long
     * enough to contain them. */
    if (release_flag < 0 && response.attrs_valid < 0 &&
        (int16_t)received_len >= REM_FILE_UNLOCK_DTU_REPLY) {
        dts_flags |= AST_SET_DTS_DTU;
    }

    /* 0x00E61DF0-0x00E61E08.  The AOTE is found by the UID at
     * location_block+0x08, and AST_$SET_DTS' own status goes to a local that
     * this function never looks at. */
    if (dts_flags != 0) {
        AST_$SET_DTS(dts_flags, &location_block->uid,
                     &response.dtv_high,
                     &response.dtu_high,
                     &dts_status);
    }

    /* 0x00E61E0E: the result byte FILE_$PRIV_UNLOCK returned on the server. */
    return response.result;
}
