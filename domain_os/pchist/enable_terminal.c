/*
 * PCHIST_$ENABLE_TERMINAL - Enable/disable terminal profiling display
 *
 * Reverse engineered from Domain/OS at address 0x00e5ca00
 */

#include "pchist/pchist_internal.h"

/*
 * External references
 */
extern pchist_control_t PCHIST_$CONTROL;

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
    status_$t status;

    /*
     * Calculate total profiling count
     * (system-wide + per-process)
     */
    total_count = PCHIST_$CONTROL.sys_profiling_count +
                  PCHIST_$CONTROL.proc_profiling_count;

    /* Notify terminal subsystem of profiling state */
    TERM_$PCHIST_ENABLE(&total_count, &status);

    /*
     * When enabling (disabling == 0) and this is the first
     * profiling activation (total_count == 1), the original
     * code displays a message. The message content is at
     * addresses 0xe5ca52-0xe5ca57 but appears to be empty
     * in this ROM image.
     */
    if (disabling == 0 && total_count == 1) {
        /*
         * TODO: Original code calls TERM_$WRITE here with
         * a message - message content unknown
         */
    }
}
