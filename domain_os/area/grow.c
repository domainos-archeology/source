/*
 * AREA_$GROW - Grow an area's virtual size
 * AREA_$GROW_TO - Grow area to specified size
 *
 * Original addresses:
 *   AREA_$GROW: 0x00E08BE8
 *   AREA_$GROW_TO: 0x00E08CEA
 */

#include "area/area_internal.h"

/*
 * AREA_$GROW - Grow an area's virtual size
 *
 * Increases the virtual size of an area. The caller must own the area
 * or the area must have remote_uid set.
 *
 * Parameters:
 *   gen          - Area generation (high word of handle)
 *   area_id      - Area ID (low word of handle)
 *   virt_size    - New virtual size in bytes
 *   commit_size  - New committed size in bytes
 *   status_ret   - Output: status code
 *
 * Original address: 0x00E08BE8
 */
void AREA_$GROW(int16_t gen, uint16_t area_id, uint32_t virt_size,
                uint32_t commit_size, status_$t *status_ret)
{
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
        entry->generation != gen) {
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

    /* Perform the resize */
    area_$resize(area_id, entry, virt_size, commit_size, 1, &status);

    ML_$LOCK(ML_LOCK_AREA);

    /* Clear in-transition flag */
    entry->flags &= ~AREA_FLAG_IN_TRANS;

    /* Advance in-transition event count to wake waiters */
    EC_$ADVANCE(&AREA_$IN_TRANS_EC);

    ML_$UNLOCK(ML_LOCK_AREA);

    *status_ret = status;
}

/*
 * AREA_$GROW_TO - Grow area to specified size
 *
 * Similar to AREA_$GROW but with slightly different validation.
 * Used when the target size is known exactly.
 *
 * Original address: 0x00E08CEA
 */
void AREA_$GROW_TO(int16_t gen, uint16_t area_id, uint32_t virt_size,
                   uint32_t commit_size, status_$t *status_ret)
{
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
        entry->generation != gen) {
        status = status_$area_not_active;
        ML_$UNLOCK(ML_LOCK_AREA);
        *status_ret = status;
        return;
    }

    /* Check ownership */
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

    /* Perform the resize */
    area_$resize(area_id, entry, virt_size, commit_size, 1, &status);

    ML_$LOCK(ML_LOCK_AREA);

    /* Clear in-transition flag */
    entry->flags &= ~AREA_FLAG_IN_TRANS;

    /* Advance in-transition event count */
    EC_$ADVANCE(&AREA_$IN_TRANS_EC);

    ML_$UNLOCK(ML_LOCK_AREA);

    *status_ret = status;
}
