/*
 * HINT_$add_internal - Add hint to the hint file (internal)
 *
 * Adds or updates a hint entry in the hint file. Called by all public
 * add functions after acquiring the exclusion lock.
 *
 * Original address: 0x00E49A2C
 */

#include "hint/hint_internal.h"

void HINT_$add_internal(uid_t *uid_ptr, hint_addr_t *addresses)
{
    uint32_t uid_key;
    hint_file_t *hintfile;
    hint_bucket_t *bucket;
    hint_slot_t *slot;
    int16_t slot_idx;
    int16_t free_slot;
    int16_t search_idx;
    uint32_t new_uid_low_masked;

    /* Early exit if no addresses provided or hint file not mapped */
    if (addresses->node_id == 0) {
        return;
    }
    if (HINT_$HINTFILE_PTR == NULL) {
        return;
    }

    hintfile = HINT_$HINTFILE_PTR;

    /* Extract the hint key from the UID low word */
    uid_key = uid_ptr->low & HINT_UID_MASK;

    /* Calculate hash bucket */
    bucket = &hintfile->buckets[uid_key & HINT_HASH_MASK];

    /* Search for existing entry or empty slot */
    free_slot = 0;
    search_idx = 1;

    for (slot_idx = HINT_SLOTS_PER_BUCKET; slot_idx >= 0; slot_idx--) {
        slot = &bucket->slots[slot_idx];

        /* Check if this slot matches our UID */
        if (uid_key == slot->uid_low_masked) {
            /* Found matching slot - update it */

            /* Check if first address matches */
            if (slot->addrs[0].node_id == addresses->node_id) {
                /* Just update the flags */
                slot->addrs[0].flags = addresses->flags;
                return;
            }

            /* Check if second address matches */
            if (slot->addrs[1].node_id == addresses->node_id) {
                /* Swap with first entry, shift others down */
                slot->addrs[2].flags = slot->addrs[1].flags;
                slot->addrs[2].node_id = slot->addrs[1].node_id;
            }

            /* Shift entries and insert new one at front */
            slot->addrs[1].flags = slot->addrs[0].flags;
            slot->addrs[1].node_id = slot->addrs[0].node_id;
            slot->addrs[0].flags = addresses->flags;
            slot->addrs[0].node_id = addresses->node_id;
            return;
        }

        /* Track first empty slot */
        if (free_slot == 0 && slot->uid_low_masked == 0) {
            free_slot = search_idx;
        }

        search_idx++;
    }

    /* Check if this is a self-hint with no actual network reference */
    if (uid_key == addresses->node_id) {
        if (addresses->flags == 0 || addresses->flags == ROUTE_$PORT) {
            return;  /* Don't add self-referential hints */
        }
    }

    /* Need to add new entry */
    if (free_slot == 0) {
        /* No free slot - use round-robin replacement */
        free_slot = HINT_$BUCKET_INDEX;
        if (HINT_$BUCKET_INDEX == HINT_SLOTS_PER_BUCKET) {
            HINT_$BUCKET_INDEX = 1;
        } else {
            HINT_$BUCKET_INDEX++;
        }
    }

    /* Allocate the slot */
    slot = &bucket->slots[free_slot - 1];

    /* Initialize the slot */
    slot->uid_low_masked = uid_key;
    slot->addrs[0].flags = addresses->flags;
    slot->addrs[0].node_id = addresses->node_id;

    /* Handle self-referential case */
    if (uid_key == addresses->node_id) {
        slot->addrs[1].flags = 0;
        slot->addrs[1].node_id = 0;
    } else {
        /* Add secondary hint with UID pointing to itself */
        slot->addrs[1].flags = addresses->flags;
        slot->addrs[1].node_id = uid_key;

        /* Recursively add the reverse hint */
        new_uid_low_masked = addresses->node_id | (uid_ptr->low & ~HINT_UID_MASK);
        {
            uid_t temp_uid;
            temp_uid.high = uid_ptr->high;
            temp_uid.low = new_uid_low_masked;
            HINT_$add_internal(&temp_uid, addresses);
        }
    }

    /* Clear remaining slots */
    slot->addrs[2].flags = 0;
    slot->addrs[2].node_id = 0;
}
