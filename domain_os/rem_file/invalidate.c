/*
 * REM_FILE_$INVALIDATE - Invalidate pages in a remote file
 *
 * Sends an invalidate request to a remote file server to mark specified pages
 * as invalid, forcing them to be re-fetched from the server on next access.
 *
 * Original address: 0x00E623D8..0x00E62456 (128 bytes)
 * A5 = 0x00E823FC, the REM_FILE module base (lea at 0x00E623E0).
 *
 * Frame: "link.w A6,-0x178" plus a two-register movem, so the epilogue is
 * "movem.l (-0x180,A6)" (0x00E6244E).  The request record base is A6-0x170
 * ("pea (-0x170,A6)" at 0x00E62442).
 *
 * Arguments (A6 displacements):
 *   0x08 vol_uid  long  (0x00E62446)
 *   0x0C uid      long  (0x00E623FE)
 *   0x10 start    long  (0x00E623E6, into D2)
 *   0x14 count    long  (0x00E623EA, into D1)
 *   0x18 flags    byte  ("move.b (0x18,A6),D0b" at 0x00E623EE - the HIGH byte
 *                        of the word slot, i.e. a byte/boolean parameter, not
 *                        the low half of an int16_t)
 *   0x1A status   long  (0x00E6241A)
 */

#include "rem_file/rem_file_internal.h"

/*
 * Invalidate request record - 0x16 bytes ("move.w #0x16,-(SP)" at
 * 0x00E6243E).
 *
 * Stores, with the listing address and the A6 displacement used:
 *   +0x02 magic     0x00E623F2  (-0x16e)
 *   +0x03 opcode    0x00E623F8  (-0x16d)
 *   +0x04 file_uid  0x00E62402/0x00E62406 (-0x16c, -0x168)
 *   +0x0C start     0x00E6240A  (-0x164)
 *   +0x10 count     0x00E6240E  (-0x160)
 *   +0x14 flags     0x00E62412  (-0x15c), a single byte
 */
typedef struct rem_file_invalidate_req_t {
    uint16_t msg_type;      /* 0x00: set to 1 by SEND_REQUEST */
    uint8_t  magic;         /* 0x02: 0x80 */
    uint8_t  opcode;        /* 0x03: 0x20 = invalidate */
    uid_t    file_uid;      /* 0x04 */
    uint32_t start;         /* 0x0C: starting page offset */
    uint32_t count;         /* 0x10: number of pages to invalidate */
    boolean  flags;         /* 0x14 */
    uint8_t  pad_15;        /* 0x15: never stored, sent as-is */
} __attribute__((packed, aligned(2))) rem_file_invalidate_req_t;

_Static_assert(__builtin_offsetof(rem_file_invalidate_req_t, magic) == 0x02, "invalidate_req.magic");
_Static_assert(__builtin_offsetof(rem_file_invalidate_req_t, opcode) == 0x03, "invalidate_req.opcode");
_Static_assert(__builtin_offsetof(rem_file_invalidate_req_t, file_uid) == 0x04, "invalidate_req.file_uid");
_Static_assert(__builtin_offsetof(rem_file_invalidate_req_t, start) == 0x0C, "invalidate_req.start");
_Static_assert(__builtin_offsetof(rem_file_invalidate_req_t, count) == 0x10, "invalidate_req.count");
_Static_assert(__builtin_offsetof(rem_file_invalidate_req_t, flags) == 0x14, "invalidate_req.flags");
_Static_assert(sizeof(rem_file_invalidate_req_t) == REM_FILE_INVALIDATE_REQ_LEN, "invalidate_req size");

void REM_FILE_$INVALIDATE(uid_t *vol_uid, uid_t *uid, uint32_t start,
                          uint32_t count, boolean flags, status_$t *status)
{
    rem_file_invalidate_req_t request;
    uint8_t response[REM_FILE_RESPONSE_BUF_SIZE];
    uint16_t received_len;      /* A6-0x176, argument 8  (0x00E6242C) */
    uint16_t packet_id;         /* A6-0x174, argument 12 (0x00E6241E) */
    uint16_t zero = 0;          /* A6-0x172, cleared at 0x00E62416 */

    /* Build request */
    request.magic = REM_FILE_REQ_MAGIC;
    request.opcode = REM_FILE_OP_INVALIDATE;  /* 0x20, 0x00E623F8 */
    request.file_uid = *uid;
    request.start = start;
    request.count = count;
    request.flags = flags;

    /* Arguments 4, 9 and 11 all point at the single zero word A6-0x172. */
    REM_FILE_$SEND_REQUEST(vol_uid, &request, REM_FILE_INVALIDATE_REQ_LEN,
                           &zero, 0,
                           response, REM_FILE_RESPONSE_BUF_SIZE,
                           &received_len, &zero, 0,
                           (int16_t *)&zero, &packet_id,
                           status);
}
