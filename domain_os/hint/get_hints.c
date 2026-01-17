/*
 * HINT_$GET_HINTS - Get hints for a remote file
 *
 * Retrieves hint information for a remote file UID. Searches the hint
 * hash table for entries matching the low 20 bits of the UID.
 *
 * Original address: 0x00E49966
 */

#include "hint/hint_internal.h"

int16_t HINT_$GET_HINTS(uid_t *lookup_uid, uint32_t *addresses)
{
    uint32_t uid_key;
    hint_file_t *hintfile;
    hint_bucket_t *bucket;
    hint_slot_t *slot;
    uint32_t *out_ptr;
    int16_t count;
    int16_t slot_idx;
    int16_t addr_idx;
    int8_t found_self;

    count = 1;
    found_self = 0;

    /* Extract the hint key from the UID low word */
    uid_key = lookup_uid->low & HINT_UID_MASK;

    /* Early exit if key is 0 or hint file not mapped */
    if (uid_key == 0 || HINT_$HINTFILE_PTR == NULL) {
        goto finish;
    }

    hintfile = HINT_$HINTFILE_PTR;

    /* Calculate hash bucket index (low 6 bits of key) */
    bucket = &hintfile->buckets[uid_key & HINT_HASH_MASK];

    /* Search through slots in this bucket */
    for (slot_idx = HINT_SLOTS_PER_BUCKET; slot_idx >= 0; slot_idx--) {
        slot = &bucket->slots[slot_idx];

        /* Check if this slot matches our UID */
        if (uid_key == slot->uid_low_masked) {
            /* Found matching slot - copy addresses */
            out_ptr = addresses;

            for (addr_idx = HINT_ADDRS_PER_SLOT; addr_idx >= 0; addr_idx--) {
                /* Stop if we hit an empty address entry */
                if (slot->addrs[addr_idx].node_id == 0) {
                    break;
                }

                /* Copy the flags and node_id */
                *out_ptr++ = slot->addrs[addr_idx].flags;
                *out_ptr++ = slot->addrs[addr_idx].node_id;

                /* Check if this is a self-reference */
                if (found_self >= 0) {
                    if (uid_key == slot->addrs[addr_idx].node_id) {
                        found_self = -1;  /* Mark that we found self */
                    }
                }

                count++;
            }
            goto finish;
        }
    }

finish:
    /* If we didn't find a self-reference and key > 4, add one */
    if (found_self >= 0 && uid_key > 4) {
        addresses[(count - 1) * 2] = 0;         /* flags */
        addresses[(count - 1) * 2 + 1] = uid_key; /* node_id = uid_key */
        count++;
    }

    /* Always append the local node as the final entry */
    addresses[(count - 1) * 2] = 0;         /* flags */
    addresses[(count - 1) * 2 + 1] = NODE_$ME;  /* local node */

    return count;
}
