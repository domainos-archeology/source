/*
 * smd/move_kbd_cursor.c - SMD_$MOVE_KBD_CURSOR implementation
 *
 * Moves the keyboard cursor to a new position and clears
 * all tracking rectangles.
 *
 * Original address: 0x00E6E7FA
 */

#include "smd/smd_internal.h"

/*
 * SMD_$MOVE_KBD_CURSOR - Move keyboard cursor
 *
 * Moves the keyboard cursor to the specified position and clears
 * any existing tracking rectangles. This is typically used when
 * the keyboard focus changes to a new location.
 *
 * Parameters:
 *   pos        - Pointer to new cursor position (x, y)
 *   status_ret - Pointer to status return
 *
 * Original address: 0x00E6E7FA
 *
 * Assembly:
 *   00e6e7fa    link.w A6,0x0
 *   00e6e7fe    movem.l {  A5 A2},-(SP)
 *   00e6e802    lea (0xe82b8c).l,A5
 *   00e6e808    movea.l (0xc,A6),A2           ; A2 = status_ret
 *   00e6e80c    pea (A2)                      ; status_ret
 *   00e6e80e    move.l (0x8,A6),-(SP)         ; pos
 *   00e6e812    bsr.w 0x00e6e766              ; SET_CURSOR_POS
 *   00e6e816    addq.w #0x8,SP
 *   00e6e818    pea (A2)                      ; status_ret
 *   00e6e81a    bsr.w 0x00e6e718              ; CLR_TRK_RECT
 *   00e6e81e    movem.l (-0x8,A6),{  A2 A5}
 *   00e6e824    unlk A6
 *   00e6e826    rts
 */
void SMD_$MOVE_KBD_CURSOR(smd_cursor_pos_t *pos, status_$t *status_ret)
{
    SMD_$SET_CURSOR_POS(pos, status_ret);
    SMD_$CLR_TRK_RECT(status_ret);
}
