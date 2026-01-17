/*
 * AREA_$THREAD_BSTES - Thread BSTE entries for area
 * AREA_$REMOVE_SEG - Remove segment from area
 * AREA_$DEACTIVATE_ASTE - Deactivate AST entry for area
 *
 * Original addresses:
 *   AREA_$THREAD_BSTES: 0x00E09722
 *   AREA_$REMOVE_SEG: 0x00E09822
 *   AREA_$DEACTIVATE_ASTE: 0x00E09EF4
 */

#include "area/area_internal.h"

/*
 * AREA_$THREAD_BSTES - Thread BSTE entries for area
 *
 * Links BSTE (Backing Store Table Entry) entries for the area.
 * This sets up the initial BSTE index and segment index for the area.
 *
 * Parameters:
 *   handle_ptr  - Pointer to area handle
 *   bste_idx    - First BSTE index
 *   seg_idx     - First segment index
 *   param_4     - Unknown parameter
 *   status_ret  - Output: status code
 *
 * Original address: 0x00E09722
 */
void AREA_$THREAD_BSTES(area_$handle_t *handle_ptr, int16_t bste_idx,
                        int16_t seg_idx, uint32_t param_4,
                        status_$t *status_ret)
{
    uint16_t area_id = AREA_HANDLE_TO_ID(*handle_ptr);
    int16_t generation = AREA_HANDLE_TO_GEN(*handle_ptr);
    area_$entry_t *entry;
    int entry_offset;

    (void)param_4;  /* Unused parameter */

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
        *status_ret = status_$area_not_active;
        ML_$UNLOCK(ML_LOCK_AREA);
        return;
    }

    /* Mark area as in-transition */
    entry->flags |= AREA_FLAG_IN_TRANS;

    ML_$UNLOCK(ML_LOCK_AREA);

    *status_ret = status_$ok;

    /* If first BSTE not yet set, initialize it */
    if (entry->first_bste == -1) {
        entry->first_bste = bste_idx;

        /* For reversed areas, adjust segment index */
        if ((entry->flags & AREA_FLAG_REVERSED) != 0) {
            int seg_count = (entry->virt_size + 0x7FFF) >> 15;
            seg_idx += seg_count - 1;
        }

        entry->first_seg_index = seg_idx;
    }

    ML_$LOCK(ML_LOCK_AREA);

    /* Clear in-transition flag */
    entry->flags &= ~AREA_FLAG_IN_TRANS;

    /* Advance in-transition event count */
    EC_$ADVANCE(&AREA_$IN_TRANS_EC);

    ML_$UNLOCK(ML_LOCK_AREA);
}

/*
 * AREA_$REMOVE_SEG - Remove segment from area
 *
 * Removes a segment from the area's segment allocation bitmap.
 *
 * Parameters:
 *   area_id     - Area ID
 *   seg_idx     - Segment index to remove
 *   status_ret  - Output: status code
 *
 * Original address: 0x00E09822
 *
 * TODO: Full analysis and implementation needed.
 */
void AREA_$REMOVE_SEG(uint16_t area_id, uint16_t seg_idx,
                      status_$t *status_ret)
{
    area_$entry_t *entry;
    int entry_offset;

    /* Validate area ID */
    if (area_id == 0 || area_id > AREA_$N_AREAS) {
        *status_ret = status_$area_not_active;
        return;
    }

    /* Calculate entry offset and get entry pointer */
    entry_offset = (uint32_t)area_id * AREA_ENTRY_SIZE;
    entry = (area_$entry_t *)(AREA_TABLE_BASE + entry_offset - AREA_ENTRY_SIZE);

    ML_$LOCK(ML_LOCK_AREA);

    /* Validate area is active */
    if ((entry->flags & AREA_FLAG_ACTIVE) == 0) {
        *status_ret = status_$area_not_active;
        ML_$UNLOCK(ML_LOCK_AREA);
        return;
    }

    *status_ret = status_$ok;

    /* Clear the segment bit in the bitmap */
    if (seg_idx < 64) {
        uint32_t word = seg_idx >> 5;
        uint32_t bit = seg_idx & 0x1F;
        entry->seg_bitmap[word] &= ~(1 << bit);
    }

    /* TODO: Handle extended segment tables for areas with > 64 segments */

    ML_$UNLOCK(ML_LOCK_AREA);
}

/*
 * AREA_$DEACTIVATE_ASTE - Deactivate AST entry for area
 *
 * Deactivates the AST entry associated with the specified area.
 *
 * Parameters:
 *   area_id     - Area ID
 *   param_2     - Unknown parameter
 *   status_ret  - Output: status code
 *
 * Original address: 0x00E09EF4
 *
 * TODO: Full analysis and implementation needed.
 */
void AREA_$DEACTIVATE_ASTE(uint16_t area_id, uint32_t param_2,
                           status_$t *status_ret)
{
    area_$entry_t *entry;
    int entry_offset;

    (void)param_2;  /* TODO: Use this parameter */

    /* Validate area ID */
    if (area_id == 0 || area_id > AREA_$N_AREAS) {
        *status_ret = status_$area_not_active;
        return;
    }

    /* Calculate entry offset and get entry pointer */
    entry_offset = (uint32_t)area_id * AREA_ENTRY_SIZE;
    entry = (area_$entry_t *)(AREA_TABLE_BASE + entry_offset - AREA_ENTRY_SIZE);

    ML_$LOCK(ML_LOCK_AREA);

    /* Validate area is active */
    if ((entry->flags & AREA_FLAG_ACTIVE) == 0) {
        *status_ret = status_$area_not_active;
        ML_$UNLOCK(ML_LOCK_AREA);
        return;
    }

    /* Mark first_bste as invalid */
    entry->first_bste = -1;

    *status_ret = status_$ok;

    ML_$UNLOCK(ML_LOCK_AREA);
}
