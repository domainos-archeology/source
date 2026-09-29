/*
 * REM_FILE_$NAME_GET_ENTRYU - look one directory entry up by name on the
 * node that owns the directory
 *
 * Original address: 0x00E6209A, 286 bytes.
 * Only caller: 0x00E57D70 (which pushes six arguments, the fourth a word).
 *
 * FRAME MAP (link.w A6,-0x1a0 at 0x00E6209A), recovered against the
 * disassembly for source-fqv4.  The request is filled IN PLACE: both ACL
 * calls are handed pointers into it, and nothing is copied in afterwards.
 *
 *   A6-0x1A0            end of frame
 *   A6-0x19E   word     received_len   (SEND_REQUEST arg 8,  0x00E6215E)
 *   A6-0x19C   word     packet_id      (SEND_REQUEST arg 12, 0x00E62150)
 *   A6-0x19A   word     the single zero word the compiler reuses for
 *                       SEND_REQUEST args 4, 9 and 11 (0x00E6214A clr.w,
 *                       then 0x00E6216C / 0x00E6215A / 0x00E62154)
 *   A6-0x198   0xA2     the request                    (SEND_REQUEST arg 2)
 *   A6-0xE8    0xBE     the response buffer            (SEND_REQUEST arg 6)
 *   A6-0x28    0x24     ACL_$GET_RE_SIDS' first output (0x00E62112)
 *
 * REQUEST (0xA2 bytes; the length word pushed at 0x00E62170):
 *   +0x00 word   msg_type, stamped 1 by REM_FILE_$SEND_REQUEST
 *   +0x02 byte   0x80                              0x00E620BC
 *   +0x03 byte   0x1C, REM_FILE_OP_NAME_GET_ENTRYU  0x00E620C2
 *   +0x04 uid    the directory                     0x00E620CC/0x00E620D0
 *   +0x0C 0x20   the entry name                    0x00E620B4/0x00E620DE
 *   +0x2C word   the caller's name length          0x00E620E6
 *   +0x2E word   3                                 0x00E620EA
 *   +0x30 byte   caller is privileged              0x00E62104
 *   +0x32 word   0                                 0x00E62108
 *   +0x34 0x24   ACL_$GET_RE_SIDS writes here      0x00E6210E
 *   +0x58 0x40   ACL_$GET_PROJ_LIST writes here    0x00E62130
 *   +0x98 word   ACL_$GET_PROJ_LIST' count         0x00E62128
 *   +0x9A long   0                                 0x00E62142
 *   +0x9E long   0                                 0x00E62146
 *
 * RESPONSE (0xBE bytes; the size word pushed at 0x00E62162):
 *   +0x08 word   entry type   -> result +0x00      0x00E62198
 *   +0x2C uid    entry uid    -> result +0x02      0x00E6218C/0x00E62190
 *   +0x34 long   extra        -> result +0x0A      0x00E621A8
 */

#include "rem_file/rem_file_internal.h"
#include "acl/acl.h"

/* The request length REM_FILE_$SEND_REQUEST is given (0x00E62170). */
#define REM_FILE_NAME_GET_ENTRYU_REQ_LEN    0xA2

/* The response buffer size REM_FILE_$SEND_REQUEST is given (0x00E62162). */
#define REM_FILE_NAME_GET_ENTRYU_RESP_LEN   0xBE

/* The name field is copied with an unconditional 32-byte byte loop
 * (0x00E620DC "moveq #0x1f,D1" .. 0x00E620E2 dbf), NOT clipped to the
 * caller's length. */
#define REM_FILE_NAME_GET_ENTRYU_NAME_LEN   0x20

/* The received length that means "the server sent no extra longword". */
#define REM_FILE_NAME_GET_ENTRYU_SHORT_REPLY 0x22

/*
 * The request record, filled in place.
 *
 * The two trailing zeroed longwords sit at 0x9A and 0x9E, i.e. only
 * 2-aligned, so they are modelled as word pairs; that keeps every field at
 * its machine offset without packing the record (which would make
 * &proj_list and &proj_count unaligned-member warnings).  The C record
 * therefore rounds up to 0xA4 bytes, but only
 * REM_FILE_NAME_GET_ENTRYU_REQ_LEN bytes are ever sent, and the original
 * frame leaves 14 unused bytes after the request anyway (A6-0xF6..A6-0xE9).
 */
typedef struct {
    uint16_t    msg_type;       /* 0x00 */
    uint8_t     magic;          /* 0x02 */
    uint8_t     opcode;         /* 0x03 */
    uid_t       dir_uid;        /* 0x04 */
    char        name[REM_FILE_NAME_GET_ENTRYU_NAME_LEN];  /* 0x0C */
    uint16_t    name_len;       /* 0x2C */
    uint16_t    flags;          /* 0x2E */
    int8_t      privileged;     /* 0x30 */
    uint8_t     _pad_31;        /* 0x31 */
    uint16_t    _zero_32;       /* 0x32 */
    uint8_t     re_sids[0x24];  /* 0x34 */
    uid_t       proj_list[8];   /* 0x58 */
    int16_t     proj_count;     /* 0x98 */
    uint16_t    _zero_9a[2];    /* 0x9A: one clr.l */
    uint16_t    _zero_9e[2];    /* 0x9E: one clr.l */
} rem_file_$name_get_entryu_req_t;

_Static_assert(offsetof(rem_file_$name_get_entryu_req_t, dir_uid)    == 0x04, "req.dir_uid");
_Static_assert(offsetof(rem_file_$name_get_entryu_req_t, name)       == 0x0C, "req.name");
_Static_assert(offsetof(rem_file_$name_get_entryu_req_t, name_len)   == 0x2C, "req.name_len");
_Static_assert(offsetof(rem_file_$name_get_entryu_req_t, flags)      == 0x2E, "req.flags");
_Static_assert(offsetof(rem_file_$name_get_entryu_req_t, privileged) == 0x30, "req.privileged");
_Static_assert(offsetof(rem_file_$name_get_entryu_req_t, _zero_32)   == 0x32, "req._zero_32");
/* the three offsets source-fqv4 is about */
_Static_assert(offsetof(rem_file_$name_get_entryu_req_t, re_sids)    == 0x34, "req.re_sids");
_Static_assert(offsetof(rem_file_$name_get_entryu_req_t, proj_list)  == 0x58, "req.proj_list");
_Static_assert(offsetof(rem_file_$name_get_entryu_req_t, proj_count) == 0x98, "req.proj_count");
_Static_assert(offsetof(rem_file_$name_get_entryu_req_t, _zero_9a)   == 0x9A, "req._zero_9a");
_Static_assert(offsetof(rem_file_$name_get_entryu_req_t, _zero_9e)   == 0x9E, "req._zero_9e");
_Static_assert(sizeof(((rem_file_$name_get_entryu_req_t *)0)->re_sids)   == 0x24, "req.re_sids size");
_Static_assert(sizeof(((rem_file_$name_get_entryu_req_t *)0)->proj_list) == 0x40, "req.proj_list size");
_Static_assert(sizeof(rem_file_$name_get_entryu_req_t) >= REM_FILE_NAME_GET_ENTRYU_REQ_LEN,
               "the request record must cover the 0xA2 bytes that are sent");

/*
 * The reply.  Only three fields are read; the rest of the 0xBE bytes are
 * never touched by this routine.
 */
typedef struct {
    uint8_t     _r00[0x08];     /* 0x00 */
    uint16_t    entry_type;     /* 0x08 */
    uint8_t     _r0a[0x22];     /* 0x0A */
    uid_t       entry_uid;      /* 0x2C */
    uint32_t    extra_info;     /* 0x34 */
    uint8_t     _r38[REM_FILE_NAME_GET_ENTRYU_RESP_LEN - 0x38];  /* 0x38 */
} rem_file_$name_get_entryu_resp_t;

_Static_assert(offsetof(rem_file_$name_get_entryu_resp_t, entry_type) == 0x08, "resp.entry_type");
_Static_assert(offsetof(rem_file_$name_get_entryu_resp_t, entry_uid)  == 0x2C, "resp.entry_uid");
_Static_assert(offsetof(rem_file_$name_get_entryu_resp_t, extra_info) == 0x34, "resp.extra_info");
_Static_assert(sizeof(rem_file_$name_get_entryu_resp_t) >= REM_FILE_NAME_GET_ENTRYU_RESP_LEN,
               "the reply record must cover the 0xBE bytes that are received");

/*
 * The caller's result record (A3 = (0x16,A6)).  The uid lands at +0x02 and
 * the extra longword at +0x0A, both odd-aligned for a longword, so the
 * record is packed (0x00E62190 move.l (A0)+,(0x2,A3), 0x00E621A8
 * move.l (-0xb4,A6),(0xa,A3)).
 */
typedef struct {
    uint16_t    entry_type;     /* 0x00 */
    uid_t       entry_uid;      /* 0x02 */
    uint32_t    extra_info;     /* 0x0A */
} __attribute__((packed)) rem_file_$name_get_entryu_result_t;

_Static_assert(offsetof(rem_file_$name_get_entryu_result_t, entry_uid)  == 0x02, "result.entry_uid");
_Static_assert(offsetof(rem_file_$name_get_entryu_result_t, extra_info) == 0x0A, "result.extra_info");
_Static_assert(sizeof(rem_file_$name_get_entryu_result_t) == 0x0E, "result must be 0x0E bytes");

void REM_FILE_$NAME_GET_ENTRYU(void *addr_info, uid_t *dir_uid,
                               char *name, uint16_t name_len,
                               void *result_ptr, status_$t *status)
{
    rem_file_$name_get_entryu_result_t *result_out =
        (rem_file_$name_get_entryu_result_t *)result_ptr;

    /* A6-0x19E, A6-0x19C, A6-0x19A */
    uint16_t received_len;
    uint16_t packet_id;
    uint16_t zero_word;

    rem_file_$name_get_entryu_req_t   request;    /* A6-0x198 */
    rem_file_$name_get_entryu_resp_t  response;   /* A6-0xE8  */
    uint8_t  local_sids[0x24];                    /* A6-0x28  */

    int i;

    /* 0x00E620B4: the first longword of the name is blanked before the
     * copy below overwrites all 32 bytes of it. */
    request.name[0] = ' ';
    request.name[1] = ' ';
    request.name[2] = ' ';
    request.name[3] = ' ';

    request.magic  = REM_FILE_REQ_MAGIC;                 /* 0x00E620BC */
    request.opcode = REM_FILE_OP_NAME_GET_ENTRYU;        /* 0x00E620C2 */
    request.dir_uid = *dir_uid;                          /* 0x00E620CC */

    /* 0x00E620DC: 32 bytes unconditionally, not clipped to name_len. */
    for (i = 0; i < REM_FILE_NAME_GET_ENTRYU_NAME_LEN; i++) {
        request.name[i] = name[i];
    }

    request.name_len = name_len;                         /* 0x00E620E6 */
    request.flags    = 3;                                /* 0x00E620EA */
    /* 0x00E620F0: tst.w ACL_$UNWIRED_DATA.super_count[PROC1_$CURRENT] / sgt */
    request.privileged = REM_FILE_PROCESS_HAS_ADMIN() ? -1 : 0;
    request._zero_32 = 0;                                /* 0x00E62108 */

    /* 0x00E62116: the second output goes straight into the request. */
    ACL_$GET_RE_SIDS(local_sids, request.re_sids, status);
    if (*status != status_$ok) {
        return;
    }

    /* 0x00E62134: so do the project list and its count.  0x00E6212C
     * "pea (-0xa16,PC)" (PC = 0x00E6212E) resolves to 0x00E61718, i.e.
     * &REM_FILE_$MAX_PROJ_LIST - the max-count word, not a UID. */
    ACL_$GET_PROJ_LIST(request.proj_list,
                       (int16_t *)&REM_FILE_$MAX_PROJ_LIST,
                       &request.proj_count, status);
    if (*status != status_$ok) {
        return;
    }

    request._zero_9a[0] = 0;                             /* 0x00E62142 */
    request._zero_9a[1] = 0;
    request._zero_9e[0] = 0;                             /* 0x00E62146 */
    request._zero_9e[1] = 0;

    zero_word = 0;                                       /* 0x00E6214A */

    /* 0x00E6217C.  The one zero word is passed three times: as extra_data,
     * as bulk_data and as bulk_len. */
    REM_FILE_$SEND_REQUEST(addr_info, &request,
                           REM_FILE_NAME_GET_ENTRYU_REQ_LEN,
                           &zero_word, 0,
                           &response, REM_FILE_NAME_GET_ENTRYU_RESP_LEN,
                           &received_len,
                           &zero_word, 0, (int16_t *)&zero_word,
                           &packet_id, status);

    /* 0x00E62184: the received length is latched into D0 before the status
     * is tested, but it is only used on the success path. */
    if (*status != status_$ok) {
        return;
    }

    result_out->entry_uid  = response.entry_uid;         /* 0x00E62190 */
    result_out->entry_type = response.entry_type;        /* 0x00E62198 */

    if (received_len == REM_FILE_NAME_GET_ENTRYU_SHORT_REPLY) {
        result_out->extra_info = 0;                      /* 0x00E621A2 */
    } else {
        result_out->extra_info = response.extra_info;    /* 0x00E621A8 */
    }
}
