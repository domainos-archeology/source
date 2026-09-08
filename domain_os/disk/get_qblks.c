/*
 * DISK_$GET_QBLKS - Get queue blocks for disk I/O
 *
 * Allocates queue blocks for disk I/O operations.  This is the read-mode
 * (mode 0) wrapper around disk_$get_qblks_internal.
 *
 * Assembly (0x00E3BFF4):
 *   link.w  A6,0x0
 *   pea     (A5)
 *   lea     (0xe7a1cc).l,A5        ; DISK_$DATA
 *   move.l  (0xe,A6),-(SP)         ; qblk_tail (address of the out cell)
 *   move.l  (0xa,A6),-(SP)         ; qblk_head (address of the out cell)
 *   clr.w   -(SP)                  ; mode = 0 (read)
 *   move.w  (0x8,A6),-(SP)         ; count
 *   bsr.w   0x00e3be8a             ; disk_$get_qblks_internal
 *   movea.l (-0x4,A6),A5 ; unlk A6 ; rts
 *
 * Both out-parameters are 32-bit target VA cells: disk_$get_qblks_internal
 * writes each with one `move.l` (0x00E3BF7E and 0x00E3BFB8) and reloads the
 * tail cell with `movea.l (A2),A3` at 0x00E3BFD8.  Every caller declares a
 * four-byte cell and converts the value to a pointer itself, so the two
 * pointer types below differ only in signedness, which is how the callers
 * spell them (ast/read_area_pages.c, ast/touch_area.c,
 * pmap/flush_write_batch.c, pmap/purifier_l.c, disk/as_xfer_multi.c).
 *
 * @param count      Number of queue blocks to allocate
 * @param qblk_head  Output: VA of the head of the queue block list
 * @param qblk_tail  Output: VA of the tail of the queue block list
 *
 * Original address: 0x00E3BFF4
 */

#include "disk/disk_internal.h"

void DISK_$GET_QBLKS(int16_t count, uint32_t *qblk_head, uint32_t *qblk_tail)
{
    disk_$get_qblks_internal(count, 0, qblk_head, qblk_tail);
}
