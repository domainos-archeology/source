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
 * Similar to AREA_$DELETE but for use by remote file operations.
 *
 * Original address: 0x00E07D06
 */
void AREA_$DELETE_FROM(area_$handle_t handle, uint32_t param_2,
                       status_$t *status_ret)
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

    /* Mark area as in-transition */
    entry->flags |= AREA_FLAG_IN_TRANS;

    ML_$UNLOCK(ML_LOCK_AREA);

    /* Perform deletion (do_unlink = 0 for this variant) */
    area_$internal_delete(entry, area_id, &status, 0);

    /* Advance in-transition event count */
    EC_$ADVANCE(&AREA_$IN_TRANS_EC);

    *status_ret = status;
}
