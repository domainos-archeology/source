/*
 * xpd/init.c - XPD_$INIT (0x00E32304, 134 bytes; map `I E32304 XPD size = 90`)
 *
 * Wires the XPD_$DATA..PROC2_$DATA range, zeroes XPD_$DATA and initialises
 * its eventcounts.  A5 is set to 0x00E3514C (the module data cell at
 * 0x00E35148 + 4) and never used.
 *
 * Frame (link.w A6,-0x18; A5 A2 D2 saved):
 *   A6-0x12  2  page_count   MST_$WIRE_AREA's fifth argument
 *   A6-0x10 12  page_list    its third (up to 3 pages, the limit word)
 */

#include "xpd/xpd_internal.h"

void XPD_$INIT(void)
{
    uint16_t page_count;                /* A6-0x12 */
    uint32_t page_list[3];              /* A6-0x10 */
    int16_t i;

    /* 0x00E32312-0x00E3232C: the three cells at 0x00E3238A / 0x00E3238C /
     * 0x00E32390, all by address. */
    MST_$WIRE_AREA(&PTR_XPD_$DATA, &PTR_PROC2_$DATA, page_list,
                   &xpd_$wire_limit, &page_count);

    /* 0x00E32330-0x00E32340 */
    OS_$DATA_ZERO(XPD_$DATA, XPD_DATA_SIZE);

    /* 0x00E32342-0x00E3235A: `moveq #0x39` / `dbf` - 58 eventcounts at a
     * 0x14 stride from +0, the last two of which lie in the debugger
     * area (+0x474 and +0x488). */
    for (i = 0; i < 58; i++) {
        EC_$INIT(&XPD_TARGET(i)->ec);
    }

    /* 0x00E3235E-0x00E3237C: `moveq #0x5` / `dbf` - the six debugger slots
     * 1..6 (A2 = base + 0x10, then (0x478,A2)). */
    for (i = 1; i <= XPD_MAX_DEBUGGERS; i++) {
        EC_$INIT(&XPD_DEBUGGER(i)->ec);
    }
}
