/*
 * PCHIST_$UNWIRE_CLEANUP - Clean up wired pages and disable profiling
 *
 * Reverse engineered from Domain/OS at address 0x00e5cd02
 */

#include "pchist/pchist_internal.h"
#include "wp/wp.h"

/*
 * External references
 */
extern pchist_control_t PCHIST_$CONTROL;
extern uint32_t PCHIST_$WIRE_PAGES[];
extern int16_t PCHIST_$WIRED_COUNT;

/*
 * PCHIST_$UNWIRE_CLEANUP
 *
 * Called when system-wide histogram profiling is stopped.
 * Unwires all pages that were wired for the histogram buffer
 * and clears the enabled flags.
 */
void PCHIST_$UNWIRE_CLEANUP(void)
{
    int16_t i;
    int16_t count;

    /*
     * Check if histogram is enabled (high bit set)
     */
    if (PCHIST_$CONTROL.histogram_enabled < 0) {
        /* Clear enabled flags */
        PCHIST_$CONTROL.histogram_enabled = 0;
        PCHIST_$CONTROL.doalign = 0;

        /*
         * Unwire all pages that were wired for the histogram buffer
         */
        if (PCHIST_$WIRED_COUNT != 0) {
            count = PCHIST_$WIRED_COUNT;
            for (i = 1; count > 0; i++, count--) {
                WP_$UNWIRE(PCHIST_$WIRE_PAGES[i]);
            }
        }

        /* Clear the wired page count */
        PCHIST_$WIRED_COUNT = 0;
    }
}
