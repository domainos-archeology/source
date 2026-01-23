/*
 * MAC_OS_$CHECK_RANGE_OVERLAP - Check if packet type ranges overlap
 *
 * Checks if a new packet type range overlaps with any existing ranges
 * in the port's packet type table.
 *
 * Original address: 0x00E0B1BC
 * Original size: 70 bytes
 */

#include "mac_os/mac_os_internal.h"

/*
 * MAC_OS_$CHECK_RANGE_OVERLAP
 *
 * This function checks if a new packet type range [new_low, new_high]
 * overlaps with any existing range in the table. Two ranges overlap if:
 *   new_low <= existing_high AND existing_low <= new_high
 *
 * Parameters:
 *   new_range  - Pointer to new range:
 *                [0]: range_low (4 bytes)
 *                [1]: range_high (4 bytes)
 *   table      - Pointer to array of packet type entries
 *   count      - Number of entries in table
 *
 * Returns:
 *   0x00XX (low byte 0) - No overlap, OK to add
 *   0xXXFF (low byte 0xFF) - Overlap detected, cannot add
 *
 * Assembly notes:
 *   - Iterates backwards through table (D0 starts at count-1)
 *   - Early exit when overlap found (sets D0.b = 0xFF)
 *   - Each entry is 12 bytes (0x0C)
 */
int16_t MAC_OS_$CHECK_RANGE_OVERLAP(uint32_t *new_range, mac_os_$pkt_type_entry_t *table, int16_t count)
{
    int16_t i;
    uint32_t new_low;
    uint32_t new_high;

    if (count <= 0) {
        return 0;  /* Empty table, no overlap possible */
    }

    new_low = new_range[0];
    new_high = new_range[1];

    /* Check each existing entry */
    for (i = count - 1; i >= 0; i--) {
        uint32_t existing_low = table[i].range_low;
        uint32_t existing_high = table[i].range_high;

        /* Check for overlap: ranges overlap if new_low <= existing_high AND existing_low <= new_high */
        if (new_low <= existing_high && existing_low <= new_high) {
            /* Overlap detected - return with 0xFF in low byte */
            return (int16_t)((i << 8) | 0xFF);
        }
    }

    /* No overlap found - return with 0x00 in low byte */
    return 0;
}
