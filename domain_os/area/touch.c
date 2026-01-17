/*
 * AREA_$TOUCH - Touch area pages (bring into memory)
 * AREA_$ASSOC - Associate area with AST entry
 *
 * Original addresses:
 *   AREA_$TOUCH: 0x00E094FE
 *   AREA_$ASSOC: 0x00E096A2
 */

#include "area/area_internal.h"

/*
 * AREA_$TOUCH - Touch area pages (bring into memory)
 *
 * Ensures pages within the specified range are present in memory.
 * If the area needs to be grown to accommodate the touched range,
 * this function will grow it.
 *
 * Parameters:
 *   handle_ptr  - Pointer to area handle
 *   bste_idx    - BSTE index
 *   seg_idx     - Segment index
 *   param_4     - Unknown parameter
 *   param_5     - Unknown parameter
 *   status_p    - Output: status code
 *
 * Original address: 0x00E094FE
 */
void AREA_$TOUCH(area_$handle_t *handle_ptr, uint16_t bste_idx,
                 uint16_t seg_idx, int16_t param_4, uint32_t param_5,
                 status_$t *status_p)
{
    uint16_t area_id = AREA_HANDLE_TO_ID(*handle_ptr);
    int16_t generation = AREA_HANDLE_TO_GEN(*handle_ptr);
    area_$entry_t *entry;
    int entry_offset;
    void *aste_ptr;
    uint32_t current_pages;
    int32_t needed_pages;
    uint32_t target_size;

    /* Validate area ID */
    if (area_id == 0 || area_id > AREA_$N_AREAS) {
        *status_p = status_$area_not_active;
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

    /* Validate area is active and generation matches (or remote) */
    if ((entry->flags & AREA_FLAG_ACTIVE) == 0 ||
        (entry->generation != generation && entry->remote_uid == 0)) {
        ML_$UNLOCK(ML_LOCK_AREA);
        *status_p = status_$area_not_active;
        return;
    }

    ML_$UNLOCK(ML_LOCK_AREA);

    /* Find the ASTE for this segment */
    aste_ptr = area_$find_entry_by_uid(area_id, &bste_idx, seg_idx, status_p);

    if (*status_p != status_$ok) {
        return;
    }

    /* Calculate current size in pages */
    current_pages = entry->commit_size >> 10;

    /* Calculate pages needed based on direction */
    if ((entry->flags & AREA_FLAG_REVERSED) != 0) {
        /* Reversed area */
        needed_pages = ((uint32_t)bste_idx * 32 + 31 - seg_idx) -
                       current_pages + 1;
    } else {
        /* Normal area */
        needed_pages = (seg_idx + (uint32_t)bste_idx * 32) -
                       current_pages + 1;
    }

    /* Grow if needed */
    if (needed_pages > 0) {
        int32_t grow_pages = (needed_pages < 4) ? 4 : needed_pages;
        target_size = entry->commit_size + grow_pages * 0x400;

        if (target_size > entry->virt_size) {
            target_size = entry->virt_size;
        }

        area_$resize(area_id, entry, entry->virt_size, target_size, 1, status_p);

        if (*status_p != status_$ok) {
            /* Decrement reference count on failure */
            *(int8_t *)((char *)aste_ptr + 0x11) -= 1;
            ML_$LOCK(ML_LOCK_AST);
            return;
        }
    }

    ML_$LOCK(ML_LOCK_AST);

    /* Touch the area through AST */
    AST_$TOUCH_AREA(area_id, *(int16_t *)((char *)aste_ptr + 0x0E),
                    seg_idx, seg_idx + (uint32_t)bste_idx * 32,
                    param_5, status_p);

    /* Mark area as touched */
    entry->flags |= AREA_FLAG_TOUCHED;

    /* Decrement reference count */
    *(int8_t *)((char *)aste_ptr + 0x11) -= 1;

    /* Note: ML_LOCK_AST is held on return for further operations */
}

/*
 * AREA_$ASSOC - Associate area with AST entry
 *
 * Associates an area with an Address Space Table entry for mapping.
 *
 * Parameters:
 *   gen         - Area generation
 *   area_id     - Area ID
 *   aste_idx    - AST entry index
 *   param_4     - Unknown parameter
 *   status_ret  - Output: status code
 *
 * Original address: 0x00E096A2
 */
void AREA_$ASSOC(uint16_t gen, int16_t area_id, uint32_t aste_idx,
                 int16_t param_4, status_$t *status_ret)
{
    area_$entry_t *entry;
    int entry_offset;
    void *aste_ptr;
    uint16_t bste_idx;

    /* Validate area ID */
    if ((uint16_t)area_id == 0 || (uint16_t)area_id > AREA_$N_AREAS) {
        *status_ret = status_$area_not_active;
        return;
    }

    /* Calculate entry offset and get entry pointer */
    entry_offset = (uint32_t)(uint16_t)area_id * AREA_ENTRY_SIZE;
    entry = (area_$entry_t *)(AREA_TABLE_BASE + entry_offset - AREA_ENTRY_SIZE);

    /* Find ASTE for this area */
    bste_idx = gen;
    aste_ptr = area_$find_entry_by_uid(area_id, &bste_idx,
                                        (uint16_t)(aste_idx & 0xFFFF), status_ret);

    if (*status_ret != status_$ok) {
        return;
    }

    ML_$LOCK(ML_LOCK_AST);

    /* Associate through AST */
    AST_$ASSOC_AREA(*(int16_t *)((char *)aste_ptr + 0x0E),
                    (uint16_t)(aste_idx & 0xFFFF),
                    aste_idx,
                    status_ret);

    /* Decrement reference count */
    *(int8_t *)((char *)aste_ptr + 0x11) -= 1;

    ML_$UNLOCK(ML_LOCK_AST);
}
