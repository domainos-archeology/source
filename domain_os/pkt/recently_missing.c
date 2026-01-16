/*
 * PKT_$RECENTLY_MISSING - Check if a node is in the recently missing list
 *
 * Checks if a node has recently failed to respond to network requests.
 * Used to avoid unnecessary retries to unresponsive nodes.
 *
 * The assembly shows:
 * 1. Loop through missing_nodes array (up to PKT_$N_MISSING entries)
 * 2. Compare node_id with each entry's node_id field
 * 3. Return 0xFF if found, 0 if not found
 *
 * Original address: 0x00E128BA
 */

#include "pkt/pkt_internal.h"

int8_t PKT_$RECENTLY_MISSING(uint32_t node_id)
{
    int16_t i;
    int16_t count;
    pkt_$missing_entry_t *entry;

    count = PKT_$N_MISSING - 1;
    if (count < 0) {
        return 0;
    }

    /* Search through the missing nodes list */
    entry = &PKT_$DATA->missing_nodes[0];
    for (i = count; i >= 0; i--) {
        if (node_id == entry->node_id) {
            return (int8_t)0xFF;  /* Found in missing list */
        }
        entry++;
    }

    return 0;  /* Not in missing list */
}
