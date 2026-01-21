/*
 * smd/set_tp_reporting.c - SMD_$SET_TP_REPORTING implementation
 *
 * Sets the trackpad reporting mode.
 *
 * Original address: 0x00E6E4B0
 */

#include "smd/smd_internal.h"

/*
 * SMD_$SET_TP_REPORTING - Set trackpad reporting mode
 *
 * Sets the reporting mode for trackpad/mouse events. The reporting
 * mode controls how frequently and what types of events are reported.
 *
 * Parameters:
 *   mode       - Pointer to reporting mode value
 *   status_ret - Pointer to status return
 *
 * Original address: 0x00E6E4B0
 *
 * Assembly:
 *   00e6e4b0    link.w A6,-0x4
 *   00e6e4b4    pea (A5)
 *   00e6e4b6    lea (0xe82b8c).l,A5
 *   00e6e4bc    movea.l (0x8,A6),A0           ; A0 = mode ptr
 *   00e6e4c0    move.w (A0),D0w               ; D0 = *mode
 *   00e6e4c2    move.w D0w,(0xde,A5)          ; tp_reporting = *mode
 *   00e6e4c6    movea.l (0xc,A6),A1           ; A1 = status_ret
 *   00e6e4ca    clr.l (A1)                    ; *status_ret = 0 (status_$ok)
 *   00e6e4cc    movea.l (-0x8,A6),A5
 *   00e6e4d0    unlk A6
 *   00e6e4d2    rts
 */
void SMD_$SET_TP_REPORTING(uint16_t *mode, status_$t *status_ret)
{
    /* Set the trackpad reporting mode */
    SMD_GLOBALS.tp_reporting = *mode;

    *status_ret = status_$ok;
}
