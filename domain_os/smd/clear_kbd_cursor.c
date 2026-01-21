/*
 * smd/clear_kbd_cursor.c - SMD_$CLEAR_KBD_CURSOR implementation
 *
 * Clears the keyboard cursor by adding a tracking rectangle
 * at the cursor position.
 *
 * Original address: 0x00E6E828
 */

#include "smd/smd_internal.h"

/* Lock data address from original code */
static const uint32_t clear_kbd_cursor_lock_data = 0x00E6DFF8;

/* Keyboard cursor tracking rect ID offset at A5+0xC0 */

/*
 * SMD_$CLEAR_KBD_CURSOR - Clear keyboard cursor
 *
 * Clears the keyboard cursor by adding a tracking rectangle.
 * This effectively "hides" the keyboard focus indicator.
 *
 * Parameters:
 *   status_ret - Pointer to status return
 *
 * Original address: 0x00E6E828
 *
 * Assembly:
 *   00e6e828    link.w A6,0x0
 *   00e6e82c    pea (A5)
 *   00e6e82e    lea (0xe82b8c).l,A5
 *   00e6e834    move.l (0x8,A6),-(SP)         ; status_ret
 *   00e6e838    pea (-0x842,PC)               ; lock_data = 0x00E6DFF8
 *   00e6e83c    pea (0xc0,A5)                 ; &kbd_cursor_track_rect
 *   00e6e840    bsr.w 0x00e6e5d8              ; ADD_TRK_RECT
 *   00e6e844    movea.l (-0x4,A6),A5
 *   00e6e848    unlk A6
 *   00e6e84a    rts
 */
void SMD_$CLEAR_KBD_CURSOR(status_$t *status_ret)
{
    SMD_$ADD_TRK_RECT(&SMD_GLOBALS.kbd_cursor_track_rect,
                      (uint16_t *)&clear_kbd_cursor_lock_data,
                      status_ret);
}
