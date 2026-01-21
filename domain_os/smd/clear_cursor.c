/*
 * smd/clear_cursor.c - SMD_$CLEAR_CURSOR implementation
 *
 * Hides/clears the cursor from the display.
 * This is a wrapper around the internal cursor drawing function.
 *
 * Original address: 0x00E6E0AA
 */

#include "smd/smd_internal.h"

/*
 * SMD_$CLEAR_CURSOR - Clear cursor
 *
 * Hides the cursor. Calls the internal cursor operation function
 * with the clear_flag set to true (0xFF).
 *
 * Parameters:
 *   unit   - Pointer to display unit number
 *   pos    - Pointer to cursor position (x, y)
 *   status_ret - Pointer to status return
 *
 * Original address: 0x00E6E0AA
 *
 * Assembly:
 *   00e6e0aa    link.w A6,0x0
 *   00e6e0ae    pea (A5)
 *   00e6e0b0    lea (0xe82b8c).l,A5
 *   00e6e0b6    move.l (0x10,A6),-(SP)        ; status_ret
 *   00e6e0ba    st -(SP)                      ; clear_flag = 0xFF (clear)
 *   00e6e0bc    movea.l (0xc,A6),A0
 *   00e6e0c0    move.l (A0),-(SP)             ; *pos (position as 32-bit value)
 *   00e6e0c2    movea.l (0x8,A6),A1
 *   00e6e0c6    move.w (A1),-(SP)             ; *unit
 *   00e6e0c8    bsr.w 0x00e6dffa              ; smd_$cursor_op
 *   00e6e0cc    movea.l (-0x4,A6),A5
 *   00e6e0d0    unlk A6
 *   00e6e0d2    rts
 */
void SMD_$CLEAR_CURSOR(uint16_t *unit, smd_cursor_pos_t *pos, status_$t *status_ret)
{
    smd_$cursor_op(*unit, *(uint32_t *)pos, 0xFF, status_ret);
}
