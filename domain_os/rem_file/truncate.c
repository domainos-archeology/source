/*
 * rem_file/truncate.c - REM_FILE_$TRUNCATE (0x00E61976, 188 bytes)
 *
 * Asks a remote file server to change an object's length; the reply carries
 * the object's new modification time, which the caller gets back.
 *
 * Re-emitted against the listing for bead source-bnky.  Corrections: the
 * flags argument is the BYTE at (0x14,A6) (the caller pushes it with
 * `move.b D7b,-(SP)` at 0x00E06254, so it occupies the even half of its word
 * slot); the request fields sit at 0x0C/0x0E/0x12/0x14; and the clock comes
 * from response+0x08 / response+0x0C, the reply buffer being based at A6-0xC0.
 *
 * Frame: `link.w A6,-0x178` + `movem.l {A5 A2 D2}` (0x0C bytes), so the
 * epilogue is `movem.l (-0x184,A6)` (0x00E61A28).
 */

#include "rem_file/rem_file_internal.h"

/*
 * The request, 0x16 bytes at A6-0x170 (`move.w #0x16,-(SP)` at 0x00E619F6).
 * new_size lands at 0x0E - only 2-aligned - so the record is packed.
 */
typedef struct rem_file_$truncate_req_t {
    uint16_t    msg_type;       /* 0x00: stamped by REM_FILE_$SEND_REQUEST */
    uint8_t     magic;          /* 0x02: 0x80  (0x00E61990) */
    uint8_t     opcode;         /* 0x03: 0x08  (0x00E61996) */
    uid_t       file_uid;       /* 0x04:       (0x00E619A0) */
    uint8_t     flags;          /* 0x0C:       (0x00E619A8) */
    uint8_t     _pad_0d;        /* 0x0D */
    uint32_t    new_size;       /* 0x0E:       (0x00E619AC) */
    uint16_t    reserved;       /* 0x12: 3     (0x00E619B0) */
    int8_t      admin_flag;     /* 0x14:       (0x00E619CA) */
    uint8_t     _pad_15;        /* 0x15 */
} __attribute__((packed)) rem_file_$truncate_req_t;

_Static_assert(__builtin_offsetof(rem_file_$truncate_req_t, file_uid) == 0x04, "truncate_req.file_uid");
_Static_assert(__builtin_offsetof(rem_file_$truncate_req_t, flags) == 0x0C, "truncate_req.flags");
_Static_assert(__builtin_offsetof(rem_file_$truncate_req_t, new_size) == 0x0E, "truncate_req.new_size");
_Static_assert(__builtin_offsetof(rem_file_$truncate_req_t, reserved) == 0x12, "truncate_req.reserved");
_Static_assert(__builtin_offsetof(rem_file_$truncate_req_t, admin_flag) == 0x14, "truncate_req.admin_flag");
_Static_assert(sizeof(rem_file_$truncate_req_t) == 0x16, "truncate_req size");

/*
 * The reply.  `move.l (-0xb8,A6),(A2)` and `move.w (-0xb4,A6),(0x4,A2)` at
 * 0x00E61A14/0x00E61A18 are response+0x08 and response+0x0C.
 */
typedef struct rem_file_$truncate_resp_t {
    uint16_t    pkt_flag;       /* 0x00 */
    uint8_t     magic;          /* 0x02 */
    uint8_t     opcode;         /* 0x03 */
    status_$t   status;         /* 0x04 */
    uint32_t    dtm_high;       /* 0x08 */
    uint16_t    dtm_low;        /* 0x0C */
    uint8_t     rest[REM_FILE_RESPONSE_BUF_SIZE - 0x0E];
} rem_file_$truncate_resp_t;

_Static_assert(__builtin_offsetof(rem_file_$truncate_resp_t, dtm_high) == 0x08, "truncate_resp.dtm_high");
_Static_assert(__builtin_offsetof(rem_file_$truncate_resp_t, dtm_low) == 0x0C, "truncate_resp.dtm_low");

/* The reply length that means "the server sent a clock" (0x00E61A0E). */
#define TRUNCATE_REPLY_WITH_DTM     0x10

void REM_FILE_$TRUNCATE(uid_t *vol_uid, uid_t *uid, uint32_t new_size,
                        uint8_t flags, clock_t *dtm_out, status_$t *status)
{
    rem_file_$truncate_req_t  request;      /* A6-0x170 */
    rem_file_$truncate_resp_t response;     /* A6-0xC0  */
    uint16_t received_len;                  /* A6-0x176 */
    uint16_t packet_id;                     /* A6-0x174 */
    int16_t  nil_word = 0;                  /* A6-0x172 */

    request.magic    = REM_FILE_REQ_MAGIC;      /* 0x00E61990 */
    request.opcode   = REM_FILE_OP_TRUNCATE;    /* 0x00E61996 */
    request.file_uid = *uid;                    /* 0x00E619A0 */
    request.flags    = flags;                   /* 0x00E619A8 */
    request.new_size = new_size;                /* 0x00E619AC */
    request.reserved = 3;                       /* 0x00E619B0 */
    request.admin_flag = REM_FILE_PROCESS_HAS_ADMIN() ? true : false;

    /* 0x00E619D2-0x00E61A06 */
    REM_FILE_$SEND_REQUEST(vol_uid, &request, 0x16,
                           &nil_word, 0,
                           &response, REM_FILE_RESPONSE_BUF_SIZE,
                           &received_len, &nil_word, 0,
                           &nil_word, &packet_id,
                           status);

    /* 0x00E61A0A-0x00E61A26.  The status is NOT consulted: a short reply just
     * means the local clock is used instead. */
    if (received_len == TRUNCATE_REPLY_WITH_DTM) {
        dtm_out->high = response.dtm_high;
        dtm_out->low  = response.dtm_low;
    } else {
        TIME_$CLOCK(dtm_out);                   /* 0x00E61A22 */
    }
}
