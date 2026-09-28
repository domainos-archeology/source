/*
 * PCHIST_$UNWIRE_CLEANUP - Unwire the histogram pages and disable sampling
 *
 * Re-emitted from the image (0x00E5CD02..0x00E5CD64, 100 bytes) and
 * verified; the previous body was faithful.  A nested procedure: A5
 * (0xE2C204) is inherited and A1 carries the static link (CNTL's frame,
 * `movea.l A1,A2`), whose A6-0x18 word it borrows as its loop counter --
 * CNTL reuses that slot for its own clear loop, so nothing reads it back.
 *
 *   00e5cd0c  tst.b (0x124,A5) / bpl exit       ; histogram_enabled
 *   00e5cd12  clr.b (0x124,A5) ; clr.b (0x126,A5)
 *   00e5cd1a  tst.w PCHIST_$WIRED_COUNT (0xE8604E) / beq 0x00E5CD56
 *   00e5cd22  D2 = count - 1 ; i = 1
 *   00e5cd38  WP_$UNWIRE(*(0xE85718 + 0x4FC + i*4)) = PCHIST_$WIRE_PAGES[i]
 *   00e5cd4e  i++ ; dbf D2                      ; count iterations
 *   00e5cd56  clr.w PCHIST_$WIRED_COUNT
 *
 * Callers: PCHIST_$CNTL 0x00E5CF9C, PCHIST_$STOP_PROFILING 0x00E5CDA4.
 *
 * Original address: 0x00e5cd02
 */

#include "pchist/pchist_internal.h"
#include "wp/wp.h"

void PCHIST_$UNWIRE_CLEANUP(void)
{
    int16_t i;                   /* the borrowed A6-0x18 */
    int16_t count;               /* D2 */

    /* 0x00E5CD0C */
    if (PCHIST_$CONTROL.histogram_enabled < 0) {
        PCHIST_$CONTROL.histogram_enabled = 0;               /* 0x00E5CD12 */
        PCHIST_$CONTROL.doalign = 0;                         /* 0x00E5CD16 */

        /* 0x00E5CD1A-0x00E5CD52 */
        if (PCHIST_$WIRED_COUNT != 0) {
            count = PCHIST_$WIRED_COUNT;
            for (i = 1; count > 0; i++, count--) {
                WP_$UNWIRE(PCHIST_$WIRE_PAGES[i]);
            }
        }

        /* 0x00E5CD56 */
        PCHIST_$WIRED_COUNT = 0;
    }
}
