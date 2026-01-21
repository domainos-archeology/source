/*
 * smd/display_cursor.c - SMD_$DISPLAY_CURSOR implementation
 *
 * Makes the cursor visible at the specified position.
 * This is a wrapper around the internal cursor drawing function.
 *
 * Original address: 0x00E6E080
 */

#include "smd/smd_internal.h"

/*
 * SMD_$DISPLAY_CURSOR - Display cursor
 *
 * Makes the cursor visible at the specified position. Calls the internal
 * cursor operation function with the clear_flag set to false (0).
 *
 * Parameters:
 *   unit   - Pointer to display unit number
 *   pos    - Pointer to cursor position (x, y)
 *   status_ret - Pointer to status return
 *
 * Original address: 0x00E6E080
 *
 * Assembly:
 *   00e6e080    link.w A6,0x0
 *   00e6e084    pea (A5)
 *   00e6e086    lea (0xe82b8c).l,A5
 *   00e6e08c    move.l (0x10,A6),-(SP)        ; status_ret
 *   00e6e090    clr.w -(SP)                   ; clear_flag = 0 (display)
 *   00e6e092    movea.l (0xc,A6),A0
 *   00e6e096    move.l (A0),-(SP)             ; *pos (position as 32-bit value)
 *   00e6e098    movea.l (0x8,A6),A1
 *   00e6e09c    move.w (A1),-(SP)             ; *unit
 *   00e6e09e    bsr.w 0x00e6dffa              ; smd_$cursor_op
 *   00e6e0a2    movea.l (-0x4,A6),A5
 *   00e6e0a6    unlk A6
 *   00e6e0a8    rts
 */
void SMD_$DISPLAY_CURSOR(uint16_t *unit, smd_cursor_pos_t *pos, status_$t *status_ret)
{
    smd_$cursor_op(*unit, *(uint32_t *)pos, 0, status_ret);
}
