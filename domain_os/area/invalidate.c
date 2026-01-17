/*
 * AREA_$INVALIDATE - Invalidate area pages
 *
 * Original address: 0x00E08DD0
 */

#include "area/area_internal.h"

/*
 * AREA_$INVALIDATE - Invalidate area pages
 *
 * Invalidates pages within the specified range of an area.
 * This is used to discard pages that are no longer needed.
 *
 * For normal (non-reversed) areas, pages are invalidated from
 * the specified offset forward.
 *
 * For reversed areas (stack-like), the invalidation logic is
 * adjusted to handle the reversed page ordering.
 *
 * Parameters:
 *   gen         - Area generation
 *   area_id     - Area ID
 *   seg_idx     - Segment index
 *   page_offset - Page offset within segment
 *   count       - Number of pages to invalidate
 *   param_6     - Unknown parameter (unused?)
 *   status_ret  - Output: status code
 *
 * Original address: 0x00E08DD0
 */
void AREA_$INVALIDATE(int16_t gen, uint16_t area_id, uint16_t seg_idx,
                      uint16_t page_offset, uint32_t count,
                      int16_t param_6, status_$t *status_ret)
{
    area_$entry_t *entry;
    int entry_offset;
    status_$t status = status_$ok;
    uint32_t max_page;
    uint32_t start_page;
    uint32_t end_page;

    (void)param_6;  /* Unused parameter */

    /* Validate area ID */
    if (area_id == 0 || area_id > AREA_$N_AREAS) {
        *status_ret = status_$area_not_active;
        return;
    }

    /* Calculate entry offset and get entry pointer */
    entry_offset = (uint32_t)area_id * AREA_ENTRY_SIZE;
    entry = (area_$entry_t *)(AREA_TABLE_BASE + entry_offset - AREA_ENTRY_SIZE);

    /* Early exit if count is 0 or area not touched or has no size */
    if (count == 0 ||
        (entry->flags & AREA_FLAG_TOUCHED) == 0 ||
        entry->virt_size == 0) {
        *status_ret = status;
        return;
    }

    ML_$LOCK(ML_LOCK_AREA);

    /* Wait if area is in transition */
    while ((entry->flags & AREA_FLAG_IN_TRANS) != 0) {
        area_$wait_in_trans();
    }

    /* Validate area is active and generation matches */
    if ((entry->flags & AREA_FLAG_ACTIVE) == 0 ||
        entry->generation != gen) {
        status = status_$area_not_active;
        goto unlock_and_return;
    }

    /* Check ownership */
    if (entry->remote_uid == 0 &&
        PROC1_$AS_ID != 0 &&
        PROC1_$AS_ID != entry->owner_asid) {
        status = status_$area_not_owner;
        goto unlock_and_return;
    }

    /* Mark area as in-transition */
    entry->flags |= AREA_FLAG_IN_TRANS;

    ML_$UNLOCK(ML_LOCK_AREA);

    /* Calculate maximum page number */
    max_page = (entry->virt_size - 1) >> 10;

    /* Handle based on area direction */
    if ((entry->flags & AREA_FLAG_REVERSED) == 0) {
        /* Normal (forward) area */
        start_page = (uint32_t)page_offset + (uint32_t)seg_idx * 32;

        if (start_page <= max_page) {
            end_page = start_page + count - 1;
            if ((int32_t)max_page < (int32_t)end_page) {
                end_page = max_page;
            }

            area_$free_segments(area_id, start_page, end_page, 0, &status);
        }
    } else {
        /* Reversed area (stack-like) */
        int16_t neg_seg = -seg_idx - 1;
        uint32_t seg_start = (uint32_t)(uint16_t)neg_seg * 32;
        uint32_t seg_end = seg_start + 32 - 1 - page_offset;

        /* Adjust page_offset if beyond max_page */
        if ((int32_t)max_page < (int32_t)seg_end) {
            page_offset = 31 - ((uint16_t)max_page & 0x1F);
        }

        /* Invalidate partial first segment if needed */
        if (page_offset != 0) {
            uint32_t first_end;
            if (count < (uint32_t)(32 - page_offset)) {
                first_end = seg_start + count;
            } else {
                first_end = seg_start + 32 - (uint32_t)page_offset;
            }
            first_end--;

            area_$free_segments(area_id, seg_start, first_end, 0, &status);
            if (status != status_$ok) {
                goto finish;
            }

            neg_seg--;
            count -= (first_end - seg_start + 1);
        }

        /* Invalidate full segments */
        if (count >= 32) {
            uint32_t full_start = ((uint16_t)neg_seg + 1) * 32;
            int16_t new_neg_seg = neg_seg - (int16_t)(count >> 5);
            uint32_t full_end = full_start - 1;

            area_$free_segments(area_id, (new_neg_seg + 1) * 32, full_end, 0, &status);
            if (status != status_$ok) {
                goto finish;
            }

            count -= (full_end - (new_neg_seg + 1) * 32 + 1);
            neg_seg = new_neg_seg;
        }

        /* Invalidate partial last segment if needed */
        if (count != 0) {
            start_page = (uint32_t)(uint16_t)neg_seg * 32;
            end_page = start_page + count - 1;

            area_$free_segments(area_id, start_page, end_page, 0, &status);
        }
    }

finish:
    ML_$LOCK(ML_LOCK_AREA);

    /* Clear in-transition flag */
    entry->flags &= ~AREA_FLAG_IN_TRANS;

    /* Advance in-transition event count */
    EC_$ADVANCE(&AREA_$IN_TRANS_EC);

unlock_and_return:
    ML_$UNLOCK(ML_LOCK_AREA);
    *status_ret = status;
}
