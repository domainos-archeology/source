/*
 * REM_FILE_$SET_ATTRIBUTE - Set attribute on a remote file
 *
 * Sends an attribute change request to a remote file server.
 * Used for operations like setting timestamps, flags, etc.
 *
 * Original address: 0x00E61A32..0x00E61AAC (124 bytes)
 * A5 = 0x00E823FC, the REM_FILE module base (lea at 0x00E61A38).
 *
 * Frame: "link.w A6,-0x178" plus "pea (A5)", so the epilogue is
 * "movea.l (-0x17c,A6),A5" (0x00E61AA6).  The request record base is A6-0x170
 * ("pea (-0x170,A6)" at 0x00E61A9A).
 *
 * Arguments (A6 displacements):
 *   0x08 vol_uid    long  (0x00E61A9E)
 *   0x0C file_uid   long  (0x00E61A4E)
 *   0x10 attr_id    word  (0x00E61A3E)
 *   0x12 attr_data  long  (0x00E61A5E)
 *   0x16 status     long  (0x00E61A72)
 */

#include "rem_file/rem_file_internal.h"

/*
 * Set attribute request record - 0x42 bytes ("move.w #0x42,-(SP)" at
 * 0x00E61A96).
 *
 * Stores, with the listing address and the A6 displacement used:
 *   +0x02 magic      0x00E61A42  (-0x16e)
 *   +0x03 opcode     0x00E61A48  (-0x16d)
 *   +0x04 file_uid   0x00E61A52/0x00E61A56 (-0x16c, -0x168)
 *   +0x0C attr_id    0x00E61A5A  (-0x164)
 *   +0x0E attr_data  0x00E61A62-0x00E61A6A (-0x162), thirteen longwords
 *                    ("moveq #0xc,D1" / dbf)
 *
 * attr_data starts at an even but not longword-aligned offset, which is why
 * the record has to be packed: an unpacked struct would push it to +0x10.
 */
#define REM_FILE_SET_ATTRIBUTE_DATA_LONGS 13  /* "moveq #0xc,D1" at 0x00E61A66 */

typedef struct rem_file_set_attr_req_t {
    uint16_t msg_type;      /* 0x00: set to 1 by SEND_REQUEST */
    uint8_t  magic;         /* 0x02: 0x80 */
    uint8_t  opcode;        /* 0x03: 0x04 = set attribute */
    uid_t    file_uid;      /* 0x04 */
    uint16_t attr_id;       /* 0x0C */
    uint8_t  attr_data[REM_FILE_SET_ATTRIBUTE_DATA_LONGS * 4];  /* 0x0E: 52 */
} __attribute__((packed, aligned(2))) rem_file_set_attr_req_t;

_Static_assert(__builtin_offsetof(rem_file_set_attr_req_t, magic) == 0x02, "set_attr_req.magic");
_Static_assert(__builtin_offsetof(rem_file_set_attr_req_t, opcode) == 0x03, "set_attr_req.opcode");
_Static_assert(__builtin_offsetof(rem_file_set_attr_req_t, file_uid) == 0x04, "set_attr_req.file_uid");
_Static_assert(__builtin_offsetof(rem_file_set_attr_req_t, attr_id) == 0x0C, "set_attr_req.attr_id");
_Static_assert(__builtin_offsetof(rem_file_set_attr_req_t, attr_data) == 0x0E, "set_attr_req.attr_data");
_Static_assert(sizeof(rem_file_set_attr_req_t) == REM_FILE_SET_ATTRIBUTE_REQ_LEN, "set_attr_req size");

void REM_FILE_$SET_ATTRIBUTE(void *vol_uid, uid_t *file_uid,
                             uint16_t attr_id, void *attr_data,
                             status_$t *status)
{
    rem_file_set_attr_req_t request;
    uint8_t response[REM_FILE_RESPONSE_BUF_SIZE];
    uint16_t received_len;      /* A6-0x176, argument 8  (0x00E61A84) */
    uint16_t packet_id;         /* A6-0x174, argument 12 (0x00E61A76) */
    uint16_t zero = 0;          /* A6-0x172, cleared at 0x00E61A6E */
    uint32_t *src = (uint32_t *)attr_data;
    uint32_t *dst = (uint32_t *)request.attr_data;
    int i;

    /* Build request */
    request.magic = REM_FILE_REQ_MAGIC;
    request.opcode = REM_FILE_OP_SET_ATTRIBUTE;  /* 0x04, 0x00E61A48 */
    request.file_uid = *file_uid;
    request.attr_id = attr_id;

    for (i = 0; i < REM_FILE_SET_ATTRIBUTE_DATA_LONGS; i++) {
        dst[i] = src[i];
    }

    /* Arguments 4, 9 and 11 all point at the single zero word A6-0x172. */
    REM_FILE_$SEND_REQUEST(vol_uid, &request, REM_FILE_SET_ATTRIBUTE_REQ_LEN,
                           &zero, 0,
                           response, REM_FILE_RESPONSE_BUF_SIZE,
                           &received_len, &zero, 0,
                           (int16_t *)&zero, &packet_id,
                           status);
}
