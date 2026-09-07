/*
 * smd/disable_tracking.c - SMD_$DISABLE_TRACKING implementation
 *
 * Disables mouse/trackpad tracking for the display.
 *
 * Original address: 0x00E6E482
 */

#include "smd/smd_internal.h"

/*
 * Lock data addresses from original code:
 *   0x00E6E458 - Cursor lock data 2
 *   0x00E6D92C - Cursor lock data 1 (SMD_ACQ_LOCK_DATA)
 */
/*
 * Resolved (bead source-2c9v): these two by-reference arguments are code-region
 * constant cells, not variables holding their addresses.  The call site pushes
 *   0x00E6E49A "pea (0x1d94,A5)", 0x00E6E496 "pea (-0xb6c,PC)" -> 0x00E6D92C, 0x00E6E492 "pea (-0x3c,PC)" -> 0x00E6E458
 * ("pea (d,PC)" resolves to instruction + 2 + d), and SHOW_CURSOR dereferences
 * argument 2 as a word (0x00E6E1EA "move.w (A1),D6w") and argument 3 as a byte
 * (0x00E6E1EE "move.b (A2),D5b").  The cells hold 0xFFFF and 0xFF; the file
 * statics that used to live here held the *addresses* 0x00E6E59A / 0x00E6E458,
 * so SHOW_CURSOR read 0xE59A and 0x00 instead.  The named cells now live in
 * smd_data.c / smd_internal.h.
 */

/*
 * SMD_$DISABLE_TRACKING - Disable mouse tracking
 *
 * Disables tracking of mouse/cursor position. Clears the tracking flag
 * and updates the cursor display.
 *
 * Parameters:
 *   param1     - Unused (original signature compatibility)
 *   status_ret - Pointer to status return
 *
 * Original address: 0x00E6E482
 *
 * Assembly:
 *   00e6e482    link.w A6,0x0
 *   00e6e486    pea (A5)
 *   00e6e488    lea (0xe82b8c).l,A5
 *   00e6e48e    clr.b (0xe0,A5)               ; tracking_enabled = 0
 *   00e6e492    pea (-0x3c,PC)                ; param3: &SMD_$TRUE_DATA (0x00E6E458)
 *   00e6e496    pea (-0xb6c,PC)               ; param2: &SMD_$ACQ_LOCK_DATA (0x00E6D92C)
 *   00e6e49a    pea (0x1d94,A5)               ; param1: &default_cursor_pos
 *   00e6e49e    bsr.w 0x00e6e1cc              ; SHOW_CURSOR
 *   00e6e4a2    movea.l (0xc,A6),A0           ; A0 = status_ret
 *   00e6e4a6    clr.l (A0)                    ; *status_ret = 0 (status_$ok)
 *   00e6e4a8    movea.l (-0x4,A6),A5
 *   00e6e4ac    unlk A6
 *   00e6e4ae    rts
 */
void SMD_$DISABLE_TRACKING(uint32_t param1, status_$t *status_ret)
{
    (void)param1;  /* Unused parameter */

    /* Disable tracking flag */
    SMD_GLOBALS.tracking_enabled = 0;

    /* Update cursor display with synchronization */
    SHOW_CURSOR(&SMD_GLOBALS.cursor_pos_sentinel,
                (const int16_t *)&SMD_ACQ_LOCK_DATA, &SMD_TRUE_DATA);

    *status_ret = status_$ok;
}
