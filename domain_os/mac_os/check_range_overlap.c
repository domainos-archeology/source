/*
 * MAC_OS_$CHECK_RANGE_OVERLAP - Check if packet type ranges overlap
 *
 * Checks whether a new packet type range overlaps any existing range in the
 * port's packet type table.
 *
 * Original address: 0x00E0B1BC
 * Original size: 70 bytes
 */

#include "mac_os/mac_os_internal.h"

/*
 * MAC_OS_$CHECK_RANGE_OVERLAP
 *
 * Two ranges overlap when new_low <= existing_high AND existing_low <=
 * new_high; both comparisons are UNSIGNED longwords ("cmp.l (0x4,A2),D1 /
 * bhi" at 0x00E0B1DC and "cmp.l (A2),D2 / bcs" at 0x00E0B1E6).
 *
 * Parameters:
 *   new_range  - the two longwords of the range being added:
 *                [0] range_low, [1] range_high
 *   table      - the existing entries; the walk starts at table[0] and steps
 *                forward one 12-byte entry at a time
 *                ("lea (0xc,A1),A1" at 0x00E0B1EE)
 *   count      - number of entries in the table
 *
 * Returns: a WORD whose LOW BYTE is the Domain boolean answer - 0xFF when a
 * range overlaps, 0 when none does.  Its high byte is not an answer: the
 * image uses D0 as both the dbf counter and the result register, so
 *
 *   - count <= 0            -> D0 is still the count, "clr.b D0b" leaves
 *                              count & 0xFF00              (0x00E0B1F6)
 *   - an overlap at entry i -> D0 holds the remaining dbf count
 *                              (count - 1 - i), "st D0b" leaves
 *                              ((count - 1 - i) & 0xFF00) | 0x00FF
 *                                                          (0x00E0B1EA)
 *   - no overlap at all     -> the dbf leaves D0 = 0xFFFF, "clr.b D0b"
 *                              leaves 0xFF00               (0x00E0B1F6)
 *
 * The one caller, MAC_OS_$OPEN, reads only the byte ("tst.b D0b / bpl" at
 * 0x00E0B31C), for which all three forms agree; the whole word is
 * reproduced anyway.
 */
int16_t MAC_OS_$CHECK_RANGE_OVERLAP(uint32_t *new_range,
                                    mac_os_$pkt_type_entry_t *table,
                                    int16_t count)
{
    uint16_t result;        /* D0w: the count, then the dbf counter */
    int16_t remaining;      /* D1w */
    int16_t i;
    uint32_t new_low;
    uint32_t new_high;

    /* 0x00E0B1C8: D0w starts out holding the count itself */
    result = (uint16_t)count;

    /* 0x00E0B1CC-0x00E0B1D0: count - 1 < 0 means there is nothing to scan */
    remaining = (int16_t)(count - 1);
    if (remaining < 0) {
        /* 0x00E0B1F6 clr.b D0b - only the low byte is cleared */
        return (int16_t)(result & 0xFF00u);
    }

    new_low = new_range[0];
    new_high = new_range[1];

    /* 0x00E0B1D2: the dbf counter takes over the result register */
    result = (uint16_t)remaining;

    /*
     * 0x00E0B1D8-0x00E0B1F2: walk the table ASCENDING from entry 0.
     */
    for (i = 0; i <= remaining; i++) {
        uint32_t existing_low = table[i].range_low;
        uint32_t existing_high = table[i].range_high;

        if (new_low <= existing_high && existing_low <= new_high) {
            /* 0x00E0B1EA st D0b - only the low byte is set */
            return (int16_t)((result & 0xFF00u) | 0x00FFu);
        }

        /* 0x00E0B1F2 dbf D0w */
        result = (uint16_t)(result - 1u);
    }

    /*
     * 0x00E0B1F6: the dbf has left the counter at 0xFFFF, and clr.b clears
     * only its low byte.
     */
    return (int16_t)(result & 0xFF00u);
}
