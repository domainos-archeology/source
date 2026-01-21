/*
 * smd/enable_tracking.c - SMD_$ENABLE_TRACKING implementation
 *
 * Enables mouse/trackpad tracking for the display.
 *
 * Original address: 0x00E6E45C
 */

#include "smd/smd_internal.h"

/*
 * SMD_$ENABLE_TRACKING - Enable mouse tracking
 *
 * Enables tracking of mouse/cursor position and stores the tracking
 * window ID for later reference. When tracking is enabled, cursor
 * movements generate events.
 *
 * Parameters:
 *   window_id  - Pointer to window ID word
 *   param2     - Unused (original signature compatibility)
 *   status_ret - Pointer to status return
 *
 * Original address: 0x00E6E45C
 *
 * Assembly:
 *   00e6e45c    link.w A6,0x0
 *   00e6e460    pea (A5)
 *   00e6e462    lea (0xe82b8c).l,A5
 *   00e6e468    st (0xe0,A5)                  ; tracking_enabled = 0xFF
 *   00e6e46c    movea.l (0x8,A6),A0           ; A0 = window_id ptr
 *   00e6e470    move.w (A0),(0xe4,A5)         ; tracking_window_id = *window_id
 *   00e6e474    movea.l (0x10,A6),A1          ; A1 = status_ret
 *   00e6e478    clr.l (A1)                    ; *status_ret = 0 (status_$ok)
 *   00e6e47a    movea.l (-0x4,A6),A5
 *   00e6e47e    unlk A6
 *   00e6e480    rts
 */
void SMD_$ENABLE_TRACKING(uint16_t *window_id, uint32_t param2, status_$t *status_ret)
{
    (void)param2;  /* Unused parameter */

    /* Enable tracking flag */
    SMD_GLOBALS.tracking_enabled = (int8_t)0xFF;

    /* Store the window ID for tracking events */
    SMD_GLOBALS.tracking_window_id = *window_id;

    *status_ret = status_$ok;
}
