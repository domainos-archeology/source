/*
 * MAC_OS_$FIND_PACKET_TYPE - Find matching packet type entry
 *
 * Searches the port's packet type table for an entry that matches
 * the given packet type value.
 *
 * Original address: 0x00E0B202
 * Original size: 66 bytes
 */

#include "mac_os/mac_os_internal.h"

/*
 * MAC_OS_$FIND_PACKET_TYPE
 *
 * This function searches for a packet type entry that contains the
 * given packet type value within its range [range_low, range_high].
 *
 * Parameters:
 *   pkt_type   - Packet type value to search for (32-bit Ethernet type)
 *   table      - Pointer to array of packet type entries
 *   count      - Number of entries in table
 *
 * Returns:
 *   Index (0-based) of matching entry if found
 *   -1 if not found
 *
 * Assembly notes:
 *   - Iterates forward through table (D2 = 0 to count-1)
 *   - Checks if range_low <= pkt_type <= range_high
 *   - Each entry is 12 bytes (0x0C)
 */
int16_t MAC_OS_$FIND_PACKET_TYPE(uint32_t pkt_type, mac_os_$pkt_type_entry_t *table, int16_t count)
{
    int16_t i;

    if (count <= 0) {
        return -1;  /* Empty table */
    }

    /* Search through entries */
    for (i = 0; i < count; i++) {
        uint32_t range_low = table[i].range_low;
        uint32_t range_high = table[i].range_high;

        /* Check if pkt_type is within this range */
        if (pkt_type >= range_low && pkt_type <= range_high) {
            return i;  /* Found matching entry */
        }
    }

    return -1;  /* Not found */
}
