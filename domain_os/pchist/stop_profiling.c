/*
 * PCHIST_$STOP_PROFILING - Stop system-wide profiling
 *
 * Reverse engineered from Domain/OS at address 0x00e5cd66
 */

#include "pchist/pchist_internal.h"

/*
 * External references
 */
extern pchist_control_t PCHIST_$CONTROL;

/*
 * Flag for alignment mode
 */
extern int8_t PCHIST_$DOALIGN_FLAG;

/*
 * PCHIST_$STOP_PROFILING
 *
 * Called to stop system-wide profiling. Decrements the
 * system profiling count and notifies the terminal.
 * If this was not command 3 (align mode), also calls
 * the enable terminal function.
 *
 * Note: This function accesses the caller's stack frame
 * to check the command parameter (param_1 at offset 8
 * from the saved frame pointer).
 */
void PCHIST_$STOP_PROFILING(void)
{
    int16_t *cmd_ptr;

    /*
     * Check if system-wide profiling is active
     */
    if (PCHIST_$CONTROL.sys_profiling_count != 0) {
        ML_$EXCLUSION_START(&PCHIST_$CONTROL.lock);

        /* Decrement the system profiling count */
        PCHIST_$CONTROL.sys_profiling_count--;

        /*
         * If not in align mode (command != 3), notify terminal
         * Note: The original code accesses the caller's stack frame
         * to check if the command was 3. We approximate this by
         * checking the doalign flag instead.
         */
        if (PCHIST_$CONTROL.doalign == 0) {
            PCHIST_$ENABLE_TERMINAL(1);  /* disabling = 1 */
        }

        ML_$EXCLUSION_STOP(&PCHIST_$CONTROL.lock);
    }

    /* Unwire pages and clear enabled flag */
    PCHIST_$UNWIRE_CLEANUP();

    /* Clear the alignment flag */
    PCHIST_$DOALIGN_FLAG = 0;
}
