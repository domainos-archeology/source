/*
 * PKT_$NOTE_VISIBLE - Update node visibility status
 *
 * Updates the visibility tracking for a node based on whether it
 * responded to a request. If visible, updates the sequence number.
 * If not visible, removes the node from the missing list.
 * If the node is not in the list and is visible, adds it.
 *
 * The assembly shows a complex algorithm:
 * 1. Search for the node in the missing list
 * 2. If found and is_visible < 0 (removing): swap with last entry, decrement count
 * 3. If found and is_visible >= 0 (updating): update sequence number
 * 4. If not found and is_visible >= 0: add to list (up to max entries)
 * 5. Track the entry with the lowest sequence number for LRU replacement
 *
 * Original address: 0x00E128F6
 */

#include "pkt/pkt_internal.h"

void PKT_$NOTE_VISIBLE(uint32_t node_id, int8_t is_visible)
{
    int16_t i;
    int16_t count;
    int16_t oldest_idx;
    int16_t search_idx;
    pkt_$missing_entry_t *entry;

    oldest_idx = 1;  /* Index of entry with lowest sequence (LRU) */
    count = PKT_$N_MISSING - 1;

    if (count >= 0) {
        search_idx = 1;
        entry = &PKT_$DATA->missing_nodes[0];

        for (i = count; i >= 0; i--) {
            if (node_id == entry->node_id) {
                /* Found the node in the list */
                if (is_visible < 0) {
                    /*
                     * Node is now unreachable - remove from list.
                     * Swap with last entry and decrement count.
                     */
                    pkt_$missing_entry_t *last = &PKT_$DATA->missing_nodes[PKT_$N_MISSING - 1];
                    entry->node_id = last->node_id;
                    entry->seq_number = last->seq_number;
                    PKT_$N_MISSING = count;
                    return;
                } else {
                    /*
                     * Node responded - update its sequence number
                     * to mark it as recently seen.
                     */
                    PKT_$DATA->visibility_seq++;
                    entry->seq_number = PKT_$DATA->visibility_seq;
                    return;
                }
            }

            /* Track oldest entry for LRU replacement */
            if (entry->seq_number < PKT_$DATA->missing_nodes[oldest_idx - 1].seq_number) {
                oldest_idx = search_idx;
            }

            search_idx++;
            entry++;
        }
    }

    /* Node not found in list */
    if (is_visible < 0) {
        /* Node is unreachable but not in list - nothing to do */
        return;
    }

    /*
     * Node responded but wasn't in list - add it.
     * If list is not full, append. Otherwise, replace oldest.
     */
    if (PKT_$N_MISSING < PKT_MAX_MISSING_NODES) {
        oldest_idx = PKT_$N_MISSING + 1;
        PKT_$N_MISSING = oldest_idx;
    }

    /* Add/replace at oldest_idx position */
    PKT_$DATA->missing_nodes[oldest_idx - 1].node_id = node_id;
    PKT_$DATA->visibility_seq++;
    PKT_$DATA->missing_nodes[oldest_idx - 1].seq_number = PKT_$DATA->visibility_seq;
}
