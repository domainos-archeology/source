/*
 * PCHIST_$ENABLE_TERMINAL - Enable/disable terminal profiling display
 *
 * Re-emitted from the image (0x00E5CA00..0x00E5CA50, 82 bytes) and verified
 * block by block; the previous body was faithful.  A5 (= 0xE2C204,
 * PCHIST_$CONTROL) is inherited from the caller, never reloaded.  Frame:
 * (0x8,A6) disabling word -> D2; A6-0xA the count word; A6-0x8 status.
 * Callers: 0x00E5CB96, 0x00E5CBE4, 0x00E5CCCE (UNIX_PROFIL_CNTL),
 * PCHIST_$CNTL 0x00E5CF68, PCHIST_$STOP_PROFILING 0x00E5CD92.
 */

#include "pchist/pchist_internal.h"
#include "term/term.h"

/*
 * Constant cells in this module's code region, all passed by address with
 * `pea (d,PC)` from the TERM_$WRITE call below.  `gsk read 0xe5ca52 6`:
 *
 *   00e5ca52  00 01 00 02 2e 2e
 */
static const uint16_t pchist_$term_count_00e5ca52 = 1;
static const uint16_t pchist_$term_line_00e5ca54  = 2;
static const char     pchist_$term_text_00e5ca56[2] = { '.', '.' };

/*
 * PCHIST_$ENABLE_TERMINAL
 *
 * Called when profiling state changes to update the terminal
 * display. When profiling is first enabled (transition from
 * 0 to 1 total profiling count), may display a message.
 *
 * Parameters:
 *   disabling - 0 if enabling profiling, non-zero if disabling
 */
void PCHIST_$ENABLE_TERMINAL(int16_t disabling)
{
    int16_t total_count;
    int16_t saved_count;
    status_$t status;

    /*
     * Calculate total profiling count (system-wide + per-process).
     * 0x00E5CA0C:
     *   move.w (0x122,A5),D3w ; add.w (0x120,A5),D3w ; move.w D3w,(-0xa,A6)
     * D3 keeps the value across the call below, so the test afterwards uses
     * the count as it was computed here, not whatever TERM_$PCHIST_ENABLE
     * may have left in the cell.
     */
    total_count = (int16_t)(PCHIST_$CONTROL.proc_profiling_count +
                            PCHIST_$CONTROL.sys_profiling_count);
    saved_count = total_count;

    /* Notify terminal subsystem of profiling state */
    TERM_$PCHIST_ENABLE(&total_count, &status);

    /*
     * On the first activation, write one character to terminal line 2.
     * 0x00E5CA32:
     *   pea (-0x8,A6)   ; arg4 = &status
     *   pea (0x1a,PC)   ; arg3 -> 0x00E5CA52, the word 1  (character count)
     *   pea (0x1a,PC)   ; arg2 -> 0x00E5CA56, the text
     *   pea (0x14,PC)   ; arg1 -> 0x00E5CA54, the word 2  (terminal line)
     *   jsr TERM_$WRITE
     *
     * `gsk read 0xe5ca52 6` gives 00 01 00 02 2e 2e, so the text cell holds
     * ".." and the count is 1: a single '.' is written.  The returned status
     * is dropped.
     */
    if (disabling == 0 && saved_count == 1) {
        TERM_$WRITE((void *)&pchist_$term_line_00e5ca54,
                    (void *)pchist_$term_text_00e5ca56,
                    (unsigned short *)&pchist_$term_count_00e5ca52,
                    &status);
    }
}
