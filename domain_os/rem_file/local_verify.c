/*
 * REM_FILE_$LOCAL_VERIFY - Verify lock on remote file server
 *
 * Sends a verification request to a remote file server to verify that a lock
 * is still valid.
 *
 * Original address: 0x00E61E20..0x00E61E98 (122 bytes)
 * A5 = 0x00E823FC, the REM_FILE module base (lea at 0x00E61E28).
 *
 * Frame: "link.w A6,-0x178" plus a two-register movem, so the epilogue is
 * "movem.l (-0x180,A6)" (0x00E61E90).  The request record base is A6-0x170
 * ("pea (-0x170,A6)" at 0x00E61E84).
 *
 * Arguments (A6 displacements):
 *   0x08 addr_info   long  (0x00E61E88)
 *   0x0C lock_block  long  (0x00E61E2E)
 *   0x10 status      long  (0x00E61E5C)
 */

#include "rem_file/rem_file_internal.h"

/*
 * Local verify request record - 0x2E bytes ("move.w #0x2e,-(SP)" at
 * 0x00E61E80).
 *
 * Stores, with the listing address and the A6 displacement used:
 *   +0x02 magic      0x00E61E32  (-0x16e)
 *   +0x03 opcode     0x00E61E38  (-0x16d)
 *   +0x04 file_uid   0x00E61E40/0x00E61E44 (-0x16c, -0x168), the first two
 *                    longwords of the lock block
 *   +0x0C lock_info  0x00E61E4A-0x00E61E56 (-0x164): A1 is reset to the START
 *                    of the lock block again ("lea (A0),A1" at 0x00E61E48), so
 *                    the eight longwords ("moveq #0x7" / dbf) plus the
 *                    trailing word re-copy the UID as well - the record
 *                    carries the UID twice, once at +0x04 and once as the
 *                    first eight bytes of lock_info.
 */
#define REM_FILE_LOCAL_VERIFY_INFO_LONGS 8   /* "moveq #0x7,D0" at 0x00E61E4E */
#define REM_FILE_LOCAL_VERIFY_INFO_BYTES \
    (REM_FILE_LOCAL_VERIFY_INFO_LONGS * 4 + 2)   /* + the "move.w (A1)+,(A2)+" */

typedef struct rem_file_local_verify_req_t {
    uint16_t msg_type;      /* 0x00: set to 1 by SEND_REQUEST */
    uint8_t  magic;         /* 0x02: 0x80 */
    uint8_t  opcode;        /* 0x03: 0x1A = local verify */
    uid_t    file_uid;      /* 0x04 */
    uint8_t  lock_info[REM_FILE_LOCAL_VERIFY_INFO_BYTES];  /* 0x0C: 34 bytes */
} __attribute__((packed, aligned(2))) rem_file_local_verify_req_t;

_Static_assert(__builtin_offsetof(rem_file_local_verify_req_t, magic) == 0x02, "local_verify_req.magic");
_Static_assert(__builtin_offsetof(rem_file_local_verify_req_t, opcode) == 0x03, "local_verify_req.opcode");
_Static_assert(__builtin_offsetof(rem_file_local_verify_req_t, file_uid) == 0x04, "local_verify_req.file_uid");
_Static_assert(__builtin_offsetof(rem_file_local_verify_req_t, lock_info) == 0x0C, "local_verify_req.lock_info");
_Static_assert(sizeof(rem_file_local_verify_req_t) == REM_FILE_LOCAL_VERIFY_REQ_LEN, "local_verify_req size");

void REM_FILE_$LOCAL_VERIFY(void *addr_info, void *lock_block, status_$t *status)
{
    rem_file_local_verify_req_t request;
    uint8_t response[REM_FILE_RESPONSE_BUF_SIZE];
    uint16_t received_len;      /* A6-0x176, argument 8  (0x00E61E6E) */
    uint16_t packet_id;         /* A6-0x174, argument 12 (0x00E61E60) */
    uint16_t zero = 0;          /* A6-0x172, cleared at 0x00E61E58 */
    uint32_t *src = (uint32_t *)lock_block;
    uint32_t *dst;
    int i;

    /* Build request */
    request.magic = REM_FILE_REQ_MAGIC;
    request.opcode = REM_FILE_OP_LOCAL_VERIFY;  /* 0x1A, 0x00E61E38 */

    /* First pass: the UID, from the head of the lock block (0x00E61E40) */
    request.file_uid.high = src[0];
    request.file_uid.low = src[1];

    /* Second pass: eight longwords and a word, again from the head of the
     * lock block (0x00E61E48 resets A1). */
    dst = (uint32_t *)request.lock_info;
    for (i = 0; i < REM_FILE_LOCAL_VERIFY_INFO_LONGS; i++) {
        dst[i] = src[i];
    }
    /* "move.w (A1)+,(A2)+" (0x00E61E56) copies the next TWO BYTES of the
     * stream, which on m68k is the high half of the ninth longword.  Copying
     * them as bytes reproduces the same wire bytes on either byte order. */
    {
        const uint8_t *src_b = (const uint8_t *)lock_block;
        uint8_t *dst_b = (uint8_t *)request.lock_info;
        dst_b[REM_FILE_LOCAL_VERIFY_INFO_LONGS * 4]     =
            src_b[REM_FILE_LOCAL_VERIFY_INFO_LONGS * 4];
        dst_b[REM_FILE_LOCAL_VERIFY_INFO_LONGS * 4 + 1] =
            src_b[REM_FILE_LOCAL_VERIFY_INFO_LONGS * 4 + 1];
    }

    /* Arguments 4, 9 and 11 all point at the single zero word A6-0x172. */
    REM_FILE_$SEND_REQUEST(addr_info, &request, REM_FILE_LOCAL_VERIFY_REQ_LEN,
                           &zero, 0,
                           response, REM_FILE_RESPONSE_BUF_SIZE,
                           &received_len, &zero, 0,
                           (int16_t *)&zero, &packet_id,
                           status);
}
