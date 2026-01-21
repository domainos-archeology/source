/*
 * smd/set_tp_cursor.c - SMD_$SET_TP_CURSOR implementation
 *
 * Sets the trackpad/touchpad cursor position.
 * This is a wrapper around SMD_$LOC_EVENT.
 *
 * Original address: 0x00E6E964
 */

#include "smd/smd_internal.h"

/*
 * SMD_$SET_TP_CURSOR - Set trackpad cursor position
 *
 * Sets the trackpad/touchpad cursor to the specified position.
 * Internally calls SMD_$LOC_EVENT with event type 0 (cursor move).
 *
 * Parameters:
 *   unit     - Pointer to display unit number
 *   pos      - Pointer to cursor position (x, y)
 *   buttons  - Pointer to button state
 *
 * Original address: 0x00E6E964
 *
 * Assembly:
 *   00e6e964    link.w A6,-0x8
 *   00e6e968    movem.l {  A5 A2},-(SP)
 *   00e6e96c    lea (0xe82b8c).l,A5
 *   00e6e972    movea.l (0xc,A6),A0
 *   00e6e976    move.l (A0),(-0x4,A6)         ; local_pos = *pos
 *   00e6e97a    movea.l (0x10,A6),A1
 *   00e6e97e    move.w (A1),(-0x6,A6)         ; local_buttons = *buttons
 *   00e6e982    subq.l #0x2,SP
 *   00e6e984    move.w (-0x6,A6),-(SP)        ; buttons
 *   00e6e988    move.l (-0x4,A6),-(SP)        ; pos
 *   00e6e98c    movea.l (0x8,A6),A2
 *   00e6e990    move.w (A2),-(SP)             ; unit
 *   00e6e992    clr.w -(SP)                   ; event_type = 0
 *   00e6e994    bsr.b 0x00e6e9a0              ; LOC_EVENT
 *   00e6e996    movem.l (-0x10,A6),{  A2 A5}
 *   00e6e99c    unlk A6
 *   00e6e99e    rts
 */
void SMD_$SET_TP_CURSOR(uint16_t *unit, smd_cursor_pos_t *pos, uint16_t *buttons)
{
    SMD_$LOC_EVENT(0, *unit, *(uint32_t *)pos, *buttons);
}
