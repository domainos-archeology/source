/*
 * AREA_$DELETE - Delete an area
 * AREA_$DELETE_FROM - Delete area with specific caller context
 * area_$internal_delete - Internal area deletion helper
 * area_$wait_in_trans - Wait for area in-transition to complete
 *
 * Original addresses:
 *   AREA_$DELETE: 0x00E07C22
 *   AREA_$DELETE_FROM: 0x00E07D06
 *   area_$internal_delete: 0x00E07B50
 *   area_$wait_in_trans: 0x00E07742
 */

#include "area/area_internal.h"
#include "misc/crash_system.h"

/*
 * area_$wait_in_trans - Wait for area in-transition to complete
 *
 * Called when AREA_FLAG_IN_TRANS is set. Waits on the in-transition
 * event count until the flag is cleared.
 *
 * Original address: 0x00E07742
 */
void area_$wait_in_trans(void)
{
    int32_t wait_val;
    ec_$eventcount_t *ecs[3];

    /* Read current value and wait for advance */
    wait_val = EC_$READ(&AREA_$IN_TRANS_EC) + 1;

    ecs[0] = &AREA_$IN_TRANS_EC;
    ecs[1] = NULL;
    ecs[2] = NULL;

    EC_$WAIT(ecs, &wait_val);
}

/*
 * area_$internal_delete - Internal area deletion
 *
 * Core deletion logic. Shrinks the area to zero size (freeing segments),
 * deletes remote backing if present, and optionally unlinks from ASID list.
 *
 * Parameters:
 *   entry      - Area entry pointer
 *   area_id    - Area ID
 *   status_p   - Output: status code
 *   do_unlink  - If negative (< 0), unlink from ASID list and add to free list
 *
 * Original address: 0x00E07B50
 */
void area_$internal_delete(area_$entry_t *entry, int16_t area_id,
                           status_$t *status_p, int8_t do_unlink)
{
    uint32_t *globals = (uint32_t *)AREA_GLOBALS_BASE;

    *status_p = status_$ok;

    /* Check if area is active */
    if ((entry->flags & AREA_FLAG_ACTIVE) == 0) {
        return;
    }

    /* If area has segments, shrink to zero (free all segments) */
    if (entry->virt_size != 0) {
        area_$resize(area_id, entry, 0, 0, 0, status_p);
        if (*status_p != status_$ok) {
            return;
        }
    }

    /* If area has remote backing, delete it */
    if (entry->remote_volx != 0) {
        REM_FILE_$DELETE_AREA(AREA_$PARTNER, entry->remote_volx,
                               entry->caller_id, status_p);
        if (*status_p != status_$ok) {
            return;
        }
    }

    /* Clear active flag */
    entry->flags &= ~AREA_FLAG_ACTIVE;

    /* If do_unlink is negative, unlink from ASID list and add to free list */
    if (do_unlink < 0) {
        ML_$LOCK(ML_LOCK_AREA);

        /* Clear in-transition flag */
        entry->flags &= ~AREA_FLAG_IN_TRANS;

        /* Unlink from doubly-linked list */
        if (entry->next != NULL) {
            entry->next->prev = entry->prev;
        }

        if (entry->prev == NULL) {
            /* This was head of ASID list */
            int asid_offset = entry->owner_asid * sizeof(uint32_t);
            area_$entry_t **asid_list = (area_$entry_t **)((char *)globals + 0x4D8 + asid_offset);
            *asid_list = entry->next;
        } else {
            entry->prev->next = entry->next;
        }

        /* Add to free list */
        entry->next = AREA_$FREE_LIST;
        entry->prev = NULL;
        AREA_$FREE_LIST = entry;

        /* Increment free count */
        AREA_$N_FREE++;

        ML_$UNLOCK(ML_LOCK_AREA);
    }
}

/*
 * AREA_$DELETE - Delete an area
 *
 * Deletes the specified area, freeing all associated resources.
 * The caller must own the area (current ASID must match owner or
 * area must have remote_uid set).
 *
 * Original address: 0x00E07C22
 */
void AREA_$DELETE(area_$handle_t handle, status_$t *status_ret)
{
    uint16_t area_id = AREA_HANDLE_TO_ID(handle);
    int16_t generation = AREA_HANDLE_TO_GEN(handle);
    area_$entry_t *entry;
    int entry_offset;
    status_$t status;

    /* Validate area ID */
    if (area_id == 0 || area_id > AREA_$N_AREAS) {
        *status_ret = status_$area_not_active;
        return;
    }

    /* Calculate entry offset and get entry pointer */
    entry_offset = (uint32_t)area_id * AREA_ENTRY_SIZE;
    entry = (area_$entry_t *)(AREA_TABLE_BASE + entry_offset - AREA_ENTRY_SIZE);

    ML_$LOCK(ML_LOCK_AREA);

    /* Wait if area is in transition */
    while ((entry->flags & AREA_FLAG_IN_TRANS) != 0) {
        area_$wait_in_trans();
    }

    /* Validate area is active and generation matches */
    if ((entry->flags & AREA_FLAG_ACTIVE) == 0 ||
        entry->generation != generation) {
        status = status_$area_not_active;
        ML_$UNLOCK(ML_LOCK_AREA);
        *status_ret = status;
        return;
    }

    /* Check ownership - must be owner or area has remote_uid */
    if (entry->remote_uid == 0 &&
        PROC1_$AS_ID != 0 &&
        PROC1_$AS_ID != entry->owner_asid) {
        status = status_$area_not_owner;
        ML_$UNLOCK(ML_LOCK_AREA);
        *status_ret = status;
        return;
    }

    /* Mark area as in-transition */
    entry->flags |= AREA_FLAG_IN_TRANS;

    ML_$UNLOCK(ML_LOCK_AREA);

    /* Perform deletion */
    area_$internal_delete(entry, area_id, &status, (int8_t)-1);

    /* Advance in-transition event count to wake waiters */
    EC_$ADVANCE(&AREA_$IN_TRANS_EC);

    *status_ret = status;
}

/*
 * AREA_$DELETE_FROM - Delete area with specific caller context
 *
 * Deletes an area identified by index, remote_uid, and caller_id.
 * Used by remote file operations (rem_file server) to delete areas
 * on behalf of remote nodes.
 *
 * Unlike AREA_$DELETE (which uses handle/generation validation),
 * this function validates by matching remote_uid and caller_id fields.
 * If the area is not active or the UID/caller_id don't match, this
 * is treated as a duplicate/stale delete (not an error).
 *
 * After successful deletion, the area entry is removed from the
 * UID hash table and returned to the free list.
 *
 * Parameters:
 *   area_index  - Area table index (1-based, uint16_t)
 *   remote_uid  - Remote UID to match against entry->remote_uid (offset 0x20)
 *   caller_id   - Caller ID to match against entry->caller_id (offset 0x10)
 *   status_ret  - Output: status code
 *
 * Original address: 0x00E07D06
 */

#define UID_HASH_BUCKETS    11

void AREA_$DELETE_FROM(uint16_t area_index, uint32_t remote_uid,
                       uint32_t caller_id, status_$t *status_ret)
{
    area_$entry_t *entry;
    int entry_offset;
    uint32_t *globals = (uint32_t *)AREA_GLOBALS_BASE;

    /* Validate area index */
    if (area_index == 0 || area_index > AREA_$N_AREAS) {
        *status_ret = status_$area_not_active;
        return;
    }

    /* Calculate entry pointer (1-indexed, each entry 0x30 bytes) */
    entry_offset = (uint32_t)area_index * AREA_ENTRY_SIZE;
    entry = (area_$entry_t *)(AREA_TABLE_BASE + entry_offset - AREA_ENTRY_SIZE);

    ML_$LOCK(ML_LOCK_AREA);

    /* Wait if area is in transition */
    while ((entry->flags & AREA_FLAG_IN_TRANS) != 0) {
        area_$wait_in_trans();
    }

    /* Check active flag, remote_uid match, and caller_id match */
    if ((entry->flags & AREA_FLAG_ACTIVE) == 0 ||
        remote_uid != entry->remote_uid ||
        caller_id != entry->caller_id) {
        /* Duplicate or stale delete — not an error */
        *status_ret = status_$ok;
        AREA_$DEL_DUP++;
    } else {
        /* Mark in-transition */
        entry->flags |= AREA_FLAG_IN_TRANS;

        ML_$UNLOCK(ML_LOCK_AREA);

        /* Perform deletion (do_unlink = 0: we handle unlinking here) */
        area_$internal_delete(entry, area_index, status_ret, 0);

        ML_$LOCK(ML_LOCK_AREA);

        if (*status_ret == status_$ok) {
            /* Remove from UID hash table */
            uint16_t hash_bucket = M$OIU$WLW(remote_uid, UID_HASH_BUCKETS);
            area_$uid_hash_t *prev_hash = NULL;
            area_$uid_hash_t *hash_entry =
                (area_$uid_hash_t *)((uint32_t *)((char *)globals + 0x454))[hash_bucket];

            /* Walk hash chain to find entry matching remote_uid */
            while (hash_entry != NULL &&
                   hash_entry->first_entry->remote_uid != remote_uid) {
                prev_hash = hash_entry;
                hash_entry = hash_entry->next;
            }

            if (hash_entry == NULL) {
                CRASH_SYSTEM(&Area_Internal_Error);
            }

            /* Unlink area entry from doubly-linked list */
            if (entry->next != NULL) {
                entry->next->prev = entry->prev;
            }

            if (entry->prev == NULL) {
                /* Was head of list — update hash entry's first_entry */
                hash_entry->first_entry = entry->next;
            } else {
                entry->prev->next = entry->next;
            }

            /* If hash entry has no more areas, remove from hash chain */
            if (hash_entry->first_entry == NULL) {
                if (prev_hash == NULL) {
                    ((uint32_t *)((char *)globals + 0x454))[hash_bucket] =
                        (uint32_t)hash_entry->next;
                } else {
                    prev_hash->next = hash_entry->next;
                }

                /* Return hash entry to free pool (at offset 0x450) */
                hash_entry->next = *(area_$uid_hash_t **)((char *)globals + 0x450);
                *(area_$uid_hash_t **)((char *)globals + 0x450) = hash_entry;
            }

            /* Add area entry to free list */
            entry->next = AREA_$FREE_LIST;
            entry->prev = NULL;
            AREA_$FREE_LIST = entry;
            AREA_$N_FREE++;
        }

        /* Clear in-transition flag */
        entry->flags &= ~AREA_FLAG_IN_TRANS;

        /* Wake waiters */
        EC_$ADVANCE(&AREA_$IN_TRANS_EC);
    }

    ML_$UNLOCK(ML_LOCK_AREA);
}
