/*
 * HINT_$clear_hintfile - Clear and reinitialize hint file
 *
 * Called when the hint file needs to be reinitialized (version != 7).
 * Truncates the file to zero and fills it with the initialized structure.
 *
 * Original address: 0x00E31194
 */

#include "hint/hint_internal.h"

void HINT_$clear_hintfile(void)
{
    hint_file_t *hintfile;
    hint_bucket_t *bucket;
    hint_slot_t *slot;
    hint_addr_t *addr;
    int16_t bucket_idx;
    int16_t slot_idx;
    int16_t addr_idx;
    status_$t status;
    uint8_t truncate_result;

    /* Truncate the hint file to zero length */
    AST_$TRUNCATE(&HINT_$HINTFILE_UID, 0, 0, &truncate_result, &status);

    hintfile = HINT_$HINTFILE_PTR;

    /* Initialize the header */
    hintfile->header.version = HINT_FILE_VERSION;  /* 7 = initialized */
    hintfile->header.net_port = 0;

    /* Copy network info from ROUTE_$PORTP + 0x2E */
    {
        uint32_t *net_info = (uint32_t *)(ROUTE_$PORTP + 0x2E);
        hintfile->header.net_info = *net_info;
    }

    /* Clear all hash buckets */
    for (bucket_idx = HINT_HASH_SIZE; bucket_idx >= 0; bucket_idx--) {
        bucket = &hintfile->buckets[bucket_idx];

        for (slot_idx = HINT_SLOTS_PER_BUCKET; slot_idx >= 0; slot_idx--) {
            slot = &bucket->slots[slot_idx];

            /* Clear the UID key */
            slot->uid_low_masked = 0;

            /* Clear all address entries */
            for (addr_idx = HINT_ADDRS_PER_SLOT; addr_idx >= 0; addr_idx--) {
                slot->addrs[addr_idx].flags = 0;
                slot->addrs[addr_idx].node_id = 0;
            }
        }
    }
}
