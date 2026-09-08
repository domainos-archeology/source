/*
 * REM_FILE_$DELETE_AREA - Delete an area in a remote file
 *
 * Sends a request to delete an area (extent) in a remote file.
 *
 * Original address: 0x00E626CC..0x00E62732 (104 bytes)
 * A5 = 0x00E823FC, the REM_FILE module base (lea at 0x00E626D2).
 *
 * Frame: "link.w A6,-0x178" plus "pea (A5)", so the epilogue is
 * "movea.l (-0x17c,A6),A5" (0x00E6272C).  The request record base is A6-0x170
 * ("pea (-0x170,A6)" at 0x00E62720).
 *
 * Arguments (A6 displacements):
 *   0x08 addr_info    long  (0x00E62724)
 *   0x0C area_handle  word  (0x00E626D8)
 *   0x0E area_offset  long  (0x00E626DC)
 *   0x12 status       long  (0x00E626F8)
 */

#include "rem_file/rem_file_internal.h"

/*
 * Delete area request record - 0x1C bytes ("move.w #0x1c,-(SP)" at
 * 0x00E6271C).
 *
 * Stores, with the listing address and the A6 displacement used:
 *   +0x02 magic        0x00E626E0  (-0x16e)
 *   +0x03 opcode       0x00E626E6  (-0x16d)
 *   +0x10 area_offset  0x00E626F0  (-0x160)
 *   +0x14 area_handle  0x00E626EC  (-0x15c)
 *
 * +0x04..+0x0F and +0x16..+0x1B are inside the declared length but are never
 * stored.  Without the explicit padding and the asserts below, m68k-elf-gcc's
 * 2-byte alignment would put area_offset at +0x0E.
 */
typedef struct rem_file_delete_area_req_t {
    uint16_t msg_type;      /* 0x00: set to 1 by SEND_REQUEST */
    uint8_t  magic;         /* 0x02: 0x80 */
    uint8_t  opcode;        /* 0x03: 0x88 = delete area */
    uint8_t  unset_04[12];  /* 0x04: never stored */
    uint32_t area_offset;   /* 0x10 */
    uint16_t area_handle;   /* 0x14 */
    uint8_t  unset_16[6];   /* 0x16: never stored */
} __attribute__((packed, aligned(2))) rem_file_delete_area_req_t;

_Static_assert(__builtin_offsetof(rem_file_delete_area_req_t, magic) == 0x02, "delete_area_req.magic");
_Static_assert(__builtin_offsetof(rem_file_delete_area_req_t, opcode) == 0x03, "delete_area_req.opcode");
_Static_assert(__builtin_offsetof(rem_file_delete_area_req_t, area_offset) == 0x10, "delete_area_req.area_offset");
_Static_assert(__builtin_offsetof(rem_file_delete_area_req_t, area_handle) == 0x14, "delete_area_req.area_handle");
_Static_assert(sizeof(rem_file_delete_area_req_t) == REM_FILE_DELETE_AREA_REQ_LEN, "delete_area_req size");

void REM_FILE_$DELETE_AREA(void *addr_info, uint16_t area_handle,
                           uint32_t area_offset, status_$t *status)
{
    rem_file_delete_area_req_t request;
    uint8_t response[REM_FILE_RESPONSE_BUF_SIZE];
    uint16_t received_len;      /* A6-0x176, argument 8  (0x00E6270A) */
    uint16_t packet_id;         /* A6-0x174, argument 12 (0x00E626FC) */
    uint16_t zero = 0;          /* A6-0x172, cleared at 0x00E626F4 */

    /* Build request */
    request.magic = REM_FILE_REQ_MAGIC;
    request.opcode = REM_FILE_OP_DELETE_AREA;  /* 0x88, 0x00E626E6 */
    request.area_handle = area_handle;
    request.area_offset = area_offset;

    /* Arguments 4, 9 and 11 all point at the single zero word A6-0x172. */
    REM_FILE_$SEND_REQUEST(addr_info, &request, REM_FILE_DELETE_AREA_REQ_LEN,
                           &zero, 0,
                           response, REM_FILE_RESPONSE_BUF_SIZE,
                           &received_len, &zero, 0,
                           (int16_t *)&zero, &packet_id,
                           status);
}
