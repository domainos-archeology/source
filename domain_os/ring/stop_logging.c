/*
 * RINGLOG_$STOP_LOGGING - Stop ring logging and unwire buffer
 *
 * Stops logging, unwires any wired memory pages, and resets state.
 *
 * Original address: 0x00E721CC
 */

#include "ring/ringlog_internal.h"

/*
 * RINGLOG_$STOP_LOGGING - Internal helper to stop logging
 *
 * This function is called when stopping ring logging. It:
 * 1. Clears the logging active flag
 * 2. Unwires all pages that were wired for the buffer
 * 3. Resets the wire count
 *
 * The function uses the caller's stack frame to store a local variable,
 * which is a quirk of the original Pascal-to-m68k compilation. This is
 * represented here with a normal local variable.
 *
 * Note: The original code accessed the caller's frame pointer (A6) to
 * store a loop counter. This is typical of nested Pascal procedures that
 * share the parent's stack frame. In C, we just use a local variable.
 */
void RINGLOG_$STOP_LOGGING(void)
{
    int16_t i;
    int16_t count;

    /* Only do something if logging is currently active */
    if (RING_$LOGGING_NOW < 0) {
        /* Clear logging flag first */
        RING_$LOGGING_NOW = 0;

        /* Get number of wired pages */
        count = RINGLOG_$CTL.wire_count - 1;

        /* Unwire all pages (pages are at indices 1 through wire_count) */
        if (count >= 0) {
            for (i = 1; i <= RINGLOG_$CTL.wire_count; i++) {
                WP_$UNWIRE(RINGLOG_$CTL.wired_pages[i]);
            }
        }

        /* Reset wire count */
        RINGLOG_$CTL.wire_count = 0;
    }
}
