/*
 * flp/shutdown.c - FLP_$SHUTDOWN (0x00E3E228, 64 bytes)
 *
 * Jump-table entry +0x04, called as shutdown(controller, unit).  Marks the
 * unit inactive and returns the number of units still active.
 *
 * Frame (link.w A6,-0xc; A5 D2 saved):
 *   (0x8,A6)    ctlr    never read
 *   (0xa,A6)    unit
 *   D0          the count being returned
 *   D1          dbf counter (3 -> four units)
 *   D2          unit index
 */

#include "flp/flp_internal.h"

int16_t FLP_$SHUTDOWN(uint16_t ctlr, uint16_t unit)
{
    int16_t count;                      /* D0 */
    uint16_t i;                         /* D2 */

    (void)ctlr;

    /* 0x00E3E236-0x00E3E242: no bounds check on unit. */
    FLP_DATA.unit_active[unit] = 0;

    /* 0x00E3E246-0x00E3E25A: `moveq #0x3,D1` / `dbf` - all four units;
     * `tst.b` / `bpl` counts the Domain-true ones. */
    count = 0;
    for (i = 0; i < FLP_MAX_UNITS; i++) {
        if (FLP_DATA.unit_active[i] < 0) {
            count++;
        }
    }

    return count;
}
