/*
 * REM_FILE_$PURIFY - Purify (flush) a remote file to disk
 *
 * Sends a purify request to a remote node to flush modified pages
 * of a file to stable storage.
 *
 * Original address: 0x00E6225C..0x00E622EC (146 bytes)
 * A5 = 0x00E823FC, the REM_FILE module base (lea at 0x00E62262).
 *
 * Frame: "link.w A6,-0x178".  The request record base is A6-0x170
 * ("pea (-0x170,A6)" at 0x00E622DA), so a request field's record offset is
 * its A6 displacement plus 0x170.
 *
 * Arguments (A6 displacements):
 *   0x08 vol_uid     long   (0x00E622DE)
 *   0x0C file_uid    long   (0x00E62278)
 *   0x10 flags       long, points at a word (0x00E62284/0x00E62288)
 *   0x14 page_index  word   (0x00E62268)
 *   0x16 status      long   (0x00E622B2)
 */

#include "rem_file/rem_file_internal.h"

/*
 * Purify request record - 0x14 bytes ("move.w #0x14,-(SP)" at 0x00E622D6).
 *
 * Every store, with the listing address and the A6 displacement it used:
 *   +0x02 magic       0x00E6226C  (-0x16e)
 *   +0x03 opcode      0x00E62272  (-0x16d)
 *   +0x04 file_uid    0x00E6227C/0x00E62280 (-0x16c, -0x168)
 *   +0x0C flags       0x00E62288  (-0x164)
 *   +0x0E page_index  0x00E6228C  (-0x162)
 *   +0x10 reserved    0x00E62290  (-0x160)
 *   +0x12 admin_flag  0x00E622AA  (-0x15e)
 * +0x00 is left to REM_FILE_$SEND_REQUEST, which stamps the message type.
 */
typedef struct rem_file_purify_req_t {
    uint16_t msg_type;      /* 0x00: set to 1 by SEND_REQUEST */
    uint8_t  magic;         /* 0x02: 0x80 */
    uint8_t  opcode;        /* 0x03: 0x14 = purify */
    uid_t    file_uid;      /* 0x04 */
    uint16_t flags;         /* 0x0C */
    int16_t  page_index;    /* 0x0E */
    uint16_t reserved;      /* 0x10: literal 3 */
    boolean  admin_flag;    /* 0x12: "sgt" result, 0 or -1 */
    uint8_t  pad_13;        /* 0x13: never stored, sent as-is */
} __attribute__((packed, aligned(2))) rem_file_purify_req_t;

_Static_assert(__builtin_offsetof(rem_file_purify_req_t, magic) == 0x02, "purify_req.magic");
_Static_assert(__builtin_offsetof(rem_file_purify_req_t, opcode) == 0x03, "purify_req.opcode");
_Static_assert(__builtin_offsetof(rem_file_purify_req_t, file_uid) == 0x04, "purify_req.file_uid");
_Static_assert(__builtin_offsetof(rem_file_purify_req_t, flags) == 0x0C, "purify_req.flags");
_Static_assert(__builtin_offsetof(rem_file_purify_req_t, page_index) == 0x0E, "purify_req.page_index");
_Static_assert(__builtin_offsetof(rem_file_purify_req_t, reserved) == 0x10, "purify_req.reserved");
_Static_assert(__builtin_offsetof(rem_file_purify_req_t, admin_flag) == 0x12, "purify_req.admin_flag");
_Static_assert(sizeof(rem_file_purify_req_t) == REM_FILE_PURIFY_REQ_LEN, "purify_req size");

void REM_FILE_$PURIFY(uid_t *vol_uid, uid_t *file_uid, uint16_t *flags,
                      int16_t page_index, status_$t *status)
{
    rem_file_purify_req_t request;
    uint8_t response[REM_FILE_RESPONSE_BUF_SIZE];
    uint16_t received_len;
    uint16_t packet_id;
    uint16_t zero = 0;

    /* Build request */
    request.magic = REM_FILE_REQ_MAGIC;
    request.opcode = REM_FILE_OP_PURIFY;
    request.file_uid = *file_uid;
    request.flags = *flags;
    request.page_index = page_index;
    request.reserved = 3;
    request.admin_flag = REM_FILE_PROCESS_HAS_ADMIN() ? true : false;

    /*
     * The request length is the literal 0x14 at 0x00E622D6 - the record size,
     * not 0x16.  Arguments 4, 9 and 11 all point at the single word cleared
     * at 0x00E622AE (A6-0x172); arguments 5 and 10 are the two "clr.w -(SP)"
     * at 0x00E622D0 and 0x00E622BE.
     */
    REM_FILE_$SEND_REQUEST(vol_uid, &request, REM_FILE_PURIFY_REQ_LEN,
                           &zero, 0,
                           response, REM_FILE_RESPONSE_BUF_SIZE,
                           &received_len, &zero, 0,
                           (int16_t *)&zero, &packet_id,
                           status);
}
