/*
 * REM_FILE_$GROW_AREA - Grow an area in a remote file
 *
 * Sends a request to extend the size of an area (extent) in a remote file.
 *
 * Original address: 0x00E62734..0x00E627A6 (116 bytes)
 * A5 = 0x00E823FC, the REM_FILE module base (lea at 0x00E6273C).
 *
 * Frame: "link.w A6,-0x178" plus a two-register movem, so the epilogue is
 * "movem.l (-0x180,A6)" (0x00E6279E).  The request record base is A6-0x170
 * ("pea (-0x170,A6)" at 0x00E62792).
 *
 * Arguments (A6 displacements):
 *   0x08 addr_info     long  (0x00E62796)
 *   0x0C area_handle   word  (0x00E62742)
 *   0x0E current_size  long  (0x00E62746)
 *   0x12 new_size      long  (0x00E6274A)
 *   0x16 status        long  (0x00E6276A)
 */

#include "rem_file/rem_file_internal.h"

/*
 * Grow area request record - 0x1C bytes ("move.w #0x1c,-(SP)" at 0x00E6278E).
 *
 * Stores, with the listing address and the A6 displacement used:
 *   +0x02 magic         0x00E6274E  (-0x16e)
 *   +0x03 opcode        0x00E62754  (-0x16d)
 *   +0x0C current_size  0x00E6275E  (-0x164)
 *   +0x14 area_handle   0x00E6275A  (-0x15c)
 *   +0x18 new_size      0x00E62762  (-0x158)
 *
 * The three gaps (+0x04..+0x0B, +0x10..+0x13, +0x16..+0x17) are inside the
 * declared length but are never stored; the frame is not cleared, so whatever
 * they hold is what goes on the wire.  Explicit padding plus the asserts below
 * are what keeps the field offsets right: with a plain unpacked struct
 * m68k-elf-gcc's 2-byte alignment lands current_size at +0x0A.
 */
typedef struct rem_file_grow_area_req_t {
    uint16_t msg_type;      /* 0x00: set to 1 by SEND_REQUEST */
    uint8_t  magic;         /* 0x02: 0x80 */
    uint8_t  opcode;        /* 0x03: 0x8A = grow area */
    uint8_t  unset_04[8];   /* 0x04: never stored */
    uint32_t current_size;  /* 0x0C */
    uint8_t  unset_10[4];   /* 0x10: never stored */
    uint16_t area_handle;   /* 0x14 */
    uint8_t  unset_16[2];   /* 0x16: never stored */
    uint32_t new_size;      /* 0x18 */
} __attribute__((packed, aligned(2))) rem_file_grow_area_req_t;

_Static_assert(__builtin_offsetof(rem_file_grow_area_req_t, magic) == 0x02, "grow_area_req.magic");
_Static_assert(__builtin_offsetof(rem_file_grow_area_req_t, opcode) == 0x03, "grow_area_req.opcode");
_Static_assert(__builtin_offsetof(rem_file_grow_area_req_t, current_size) == 0x0C, "grow_area_req.current_size");
_Static_assert(__builtin_offsetof(rem_file_grow_area_req_t, area_handle) == 0x14, "grow_area_req.area_handle");
_Static_assert(__builtin_offsetof(rem_file_grow_area_req_t, new_size) == 0x18, "grow_area_req.new_size");
_Static_assert(sizeof(rem_file_grow_area_req_t) == REM_FILE_GROW_AREA_REQ_LEN, "grow_area_req size");

void REM_FILE_$GROW_AREA(void *addr_info, uint16_t area_handle,
                         uint32_t current_size, uint32_t new_size,
                         status_$t *status)
{
    rem_file_grow_area_req_t request;
    uint8_t response[REM_FILE_RESPONSE_BUF_SIZE];
    uint16_t received_len;      /* A6-0x176, argument 8  (0x00E6277C) */
    uint16_t packet_id;         /* A6-0x174, argument 12 (0x00E6276E) */
    uint16_t zero = 0;          /* A6-0x172, cleared at 0x00E62766 */

    /* Build request */
    request.magic = REM_FILE_REQ_MAGIC;
    request.opcode = REM_FILE_OP_GROW_AREA;  /* 0x8A, 0x00E62754 */
    request.area_handle = area_handle;
    request.current_size = current_size;
    request.new_size = new_size;

    /* Arguments 4, 9 and 11 all point at the single zero word A6-0x172. */
    REM_FILE_$SEND_REQUEST(addr_info, &request, REM_FILE_GROW_AREA_REQ_LEN,
                           &zero, 0,
                           response, REM_FILE_RESPONSE_BUF_SIZE,
                           &received_len, &zero, 0,
                           (int16_t *)&zero, &packet_id,
                           status);
}
