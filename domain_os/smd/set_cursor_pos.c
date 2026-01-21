/*
 * smd/set_cursor_pos.c - SMD_$SET_CURSOR_POS implementation
 *
 * Sets the cursor position for the default display unit.
 * This is a wrapper around SMD_$SET_UNIT_CURSOR_POS.
 *
 * Original address: 0x00E6E766
 */

#include "smd/smd_internal.h"

/*
 * SMD_$SET_CURSOR_POS - Set cursor position
 *
 * Sets the cursor position on the default display unit.
 * Simply calls SMD_$SET_UNIT_CURSOR_POS with the default unit.
 *
 * Parameters:
 *   pos        - Pointer to new cursor position (x, y)
 *   status_ret - Pointer to status return
 *
 * Original address: 0x00E6E766
 *
 * Assembly:
 *   00e6e766    link.w A6,0x0
 *   00e6e76a    pea (A5)
 *   00e6e76c    lea (0xe82b8c).l,A5
 *   00e6e772    move.l (0xc,A6),-(SP)         ; status_ret
 *   00e6e776    move.l (0x8,A6),-(SP)         ; pos
 *   00e6e77a    pea (0x1d98,A5)               ; &SMD_DEFAULT_DISPLAY_UNIT
 *   00e6e77e    bsr.b 0x00e6e788              ; SET_UNIT_CURSOR_POS
 *   00e6e780    movea.l (-0x4,A6),A5
 *   00e6e784    unlk A6
 *   00e6e786    rts
 */
void SMD_$SET_CURSOR_POS(smd_cursor_pos_t *pos, status_$t *status_ret)
{
    SMD_$SET_UNIT_CURSOR_POS(&SMD_DEFAULT_DISPLAY_UNIT, pos, status_ret);
}
