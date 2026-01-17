/*
 * AREA_$TRANSFER - Transfer area ownership to another address space
 *
 * Original address: 0x00E08098
 */

#include "area/area_internal.h"

/*
 * AREA_$TRANSFER - Transfer area ownership to another address space
 *
 * Transfers ownership of an area from the current owner to a new ASID.
 * Also updates the area's virtual size and segment index if needed.
 *
 * The caller must be the current owner of the area.
 *
 * Parameters:
 *   handle_ptr    - Pointer to area handle
 *   new_asid      - New owner ASID
 *   new_seg_idx   - New segment index
 *   new_virt_size - New virtual size
 *   status_ret    - Output: status code
 *
 * Returns: Previous first segment index
 *
 * Original address: 0x00E08098
 */
int16_t AREA_$TRANSFER(area_$handle_t *handle_ptr, int16_t new_asid,
                       int16_t new_seg_idx, uint32_t new_virt_size,
                       status_$t *status_ret)
{
    uint16_t area_id;
    int16_t generation;
    area_$entry_t *entry;
    int entry_offset;
    status_$t status = status_$ok;
    int16_t prev_seg_idx;
    uint32_t old_virt_size;
    uint32_t *globals = (uint32_t *)AREA_GLOBALS_BASE;

    area_id = AREA_HANDLE_TO_ID(*handle_ptr);
    generation = AREA_HANDLE_TO_GEN(*handle_ptr);
    prev_seg_idx = new_asid;  /* Will be updated if successful */

    /* Validate area ID */
    if (area_id == 0 || area_id > AREA_$N_AREAS) {
        *status_ret = status_$area_not_active;
        return new_asid;
    }

    /* Calculate entry offset and get entry pointer */
    entry_offset = (uint32_t)area_id * AREA_ENTRY_SIZE;
    entry = (area_$entry_t *)(AREA_TABLE_BASE + entry_offset - AREA_ENTRY_SIZE);

    ML_$LOCK(ML_LOCK_AREA);

    /* Wait if area is in transition */
    while ((entry->flags & AREA_FLAG_IN_TRANS) != 0) {
        area_$wait_in_trans();
    }

    /* Validate area is active */
    if ((entry->flags & AREA_FLAG_ACTIVE) == 0) {
        status = status_$area_not_active;
        goto unlock_and_return;
    }

    /* Check ownership - must be current owner */
    if (entry->owner_asid != PROC1_$AS_ID) {
        status = status_$area_not_owner;
        goto unlock_and_return;
    }

    /* Mark area as in-transition */
    entry->flags |= AREA_FLAG_IN_TRANS;

    ML_$UNLOCK(ML_LOCK_AREA);

    old_virt_size = entry->virt_size;

    /* If shrinking, resize first */
    if (new_virt_size < old_virt_size) {
        area_$resize(area_id, entry, new_virt_size, entry->commit_size, 1, &status);
        if (status != status_$ok) {
            ML_$LOCK(ML_LOCK_AREA);
            goto clear_and_return;
        }
    }

    /* Update BSTE linkage with new ASID lock */
    ML_$LOCK(ML_LOCK_AST);

    /* Calculate new segment adjustment for reversed areas */
    int16_t seg_adjustment = 0;
    if ((entry->flags & AREA_FLAG_REVERSED) != 0) {
        seg_adjustment = ((entry->virt_size + 0x7FFF) >> 15) - 1;
    }

    prev_seg_idx = entry->first_seg_index;
    entry->first_bste = new_asid;
    entry->first_seg_index = new_seg_idx;

    if ((entry->flags & AREA_FLAG_REVERSED) != 0) {
        entry->first_seg_index += seg_adjustment;
    }

    ML_$UNLOCK(ML_LOCK_AST);

    /* If growing, resize after transfer */
    if (old_virt_size < new_virt_size) {
        area_$resize(area_id, entry, new_virt_size, entry->commit_size, 1, &status);
        if (status != status_$ok) {
            /* Revert on failure */
            ML_$LOCK(ML_LOCK_AST);
            entry->first_bste = PROC1_$AS_ID;
            entry->first_seg_index = prev_seg_idx;
            ML_$UNLOCK(ML_LOCK_AST);

            ML_$LOCK(ML_LOCK_AREA);
            goto clear_and_return;
        }
    }

    ML_$LOCK(ML_LOCK_AREA);

    /* Unlink from old ASID's list */
    if (entry->next != NULL) {
        entry->next->prev = entry->prev;
    }
    if (entry->prev == NULL) {
        /* Was head of old ASID's list */
        int old_asid_offset = entry->owner_asid * sizeof(uint32_t);
        area_$entry_t **old_list = (area_$entry_t **)((char *)globals + 0x4D8 + old_asid_offset);
        *old_list = entry->next;
    } else {
        entry->prev->next = entry->next;
    }

    /* Link into new ASID's list */
    int new_asid_offset = new_asid * sizeof(uint32_t);
    area_$entry_t **new_list = (area_$entry_t **)((char *)globals + 0x4D8 + new_asid_offset);
    entry->next = *new_list;
    if (entry->next != NULL) {
        entry->next->prev = entry;
    }
    entry->prev = NULL;
    *new_list = entry;

    /* Update owner */
    entry->owner_asid = new_asid;
    prev_seg_idx = new_asid;  /* Return old segment index */

clear_and_return:
    /* Clear in-transition flag */
    entry->flags &= ~AREA_FLAG_IN_TRANS;

    /* Advance in-transition event count */
    EC_$ADVANCE(&AREA_$IN_TRANS_EC);

unlock_and_return:
    ML_$UNLOCK(ML_LOCK_AREA);
    *status_ret = status;
    return prev_seg_idx;
}
