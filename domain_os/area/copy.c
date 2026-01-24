/*
 * AREA_$COPY - Copy an area (copy-on-write)
 *
 * Original address: 0x00E0901A
 */

#include "area/area_internal.h"
#include "misc/crash_system.h"

/*
 * AREA_$COPY - Copy an area (copy-on-write)
 *
 * Creates a copy of an area with copy-on-write semantics.
 * Both the source and destination share physical pages until
 * one of them is written to.
 *
 * This is used during process fork to efficiently copy the
 * address space.
 *
 * Parameters:
 *   gen          - Source area generation
 *   area_id      - Source area ID
 *   new_asid     - New owner ASID for the copy
 *   param_4      - Unknown parameter
 *   stack_limit  - Stack limit address (pages above this are skipped)
 *   status_ret   - Output: status code
 *
 * Returns: New area handle
 *
 * Original address: 0x00E0901A
 */
uint32_t AREA_$COPY(int16_t gen, uint16_t area_id, int16_t new_asid,
                    int16_t param_4, uint32_t stack_limit,
                    status_$t *status_ret)
{
    area_$entry_t *src_entry;
    area_$entry_t *dst_entry;
    int src_offset;
    int dst_offset;
    uint32_t new_handle = 0;
    uint16_t new_area_id;
    int8_t is_reversed;
    uint32_t src_virt_size;
    uint32_t stack_low_page;
    uint32_t stack_high_page;
    uint16_t bitmap_bytes;
    status_$t status;
    status_$t temp_status;

    /* Validate source area ID */
    if (area_id == 0 || area_id > AREA_$N_AREAS) {
        *status_ret = status_$area_not_active;
        return 0;
    }

    /* Calculate source entry offset and get entry pointer */
    src_offset = (uint32_t)area_id * AREA_ENTRY_SIZE;
    src_entry = (area_$entry_t *)(AREA_TABLE_BASE + src_offset - AREA_ENTRY_SIZE);

    /* Validate source area is active and generation matches */
    if ((src_entry->flags & AREA_FLAG_ACTIVE) == 0 ||
        src_entry->generation != gen) {
        *status_ret = status_$area_not_active;
        return 0;
    }

    /* Check ownership */
    if (src_entry->remote_uid == 0 &&
        PROC1_$AS_ID != 0 &&
        PROC1_$AS_ID != src_entry->owner_asid) {
        *status_ret = status_$area_not_owner;
        return 0;
    }

    /* Determine if area is reversed */
    is_reversed = (src_entry->flags & AREA_FLAG_REVERSED) ? (int8_t)-1 : 0;

    /* Create the destination area */
    new_handle = area_$internal_create(
        src_entry->virt_size,
        src_entry->commit_size,
        0,              /* remote_uid = 0 */
        new_asid,
        1,              /* alloc_remote */
        is_reversed,
        status_ret
    );

    if (*status_ret != status_$ok) {
        return 0;
    }

    new_area_id = AREA_HANDLE_TO_ID(new_handle);
    dst_offset = (uint32_t)new_area_id * AREA_ENTRY_SIZE;
    dst_entry = (area_$entry_t *)(AREA_TABLE_BASE + dst_offset - AREA_ENTRY_SIZE);

    src_virt_size = src_entry->virt_size;

    /* If source has no size, we're done */
    if (src_virt_size == 0) {
        return new_handle;
    }

    /* Copy flags and remote_uid */
    dst_entry->flags = src_entry->flags;
    dst_entry->remote_uid = src_entry->remote_uid;

    /* Lock and mark source as in-transition */
    ML_$LOCK(ML_LOCK_AREA);
    while ((src_entry->flags & AREA_FLAG_IN_TRANS) != 0) {
        area_$wait_in_trans();
    }
    src_entry->flags |= AREA_FLAG_IN_TRANS;
    ML_$UNLOCK(ML_LOCK_AREA);

    /* Set up destination BSTE linkage */
    dst_entry->first_bste = new_asid;
    dst_entry->first_seg_index = src_entry->first_seg_index;

    /* Calculate bitmap size and stack page limits */
    uint32_t pages = (src_virt_size >> 10);
    bitmap_bytes = (uint16_t)((((pages + 31) >> 5) + 7) >> 3);

    stack_low_page = AS_$STACK_LOW >> 15;
    stack_high_page = stack_limit >> 15;

    /* Copy pages segment by segment */
    int16_t seg_counter = 0;
    int16_t seg_direction = is_reversed ? -1 : 1;
    uint16_t seg_page = src_entry->first_seg_index;
    uint32_t *src_bitmap = src_entry->seg_bitmap;
    uint32_t *dst_bitmap = dst_entry->seg_bitmap;

    for (uint16_t byte_idx = 0; byte_idx < bitmap_bytes; byte_idx++) {
        uint8_t *src_bitmap_ptr;
        uint8_t *dst_bitmap_ptr;

        /* Get bitmap pointers (inline for first 2 bytes, external table otherwise) */
        if (byte_idx < 2) {
            src_bitmap_ptr = (uint8_t *)&src_entry->seg_bitmap[0] + byte_idx * 4;
            dst_bitmap_ptr = (uint8_t *)&dst_entry->seg_bitmap[0] + byte_idx * 4;
        } else {
            /* Look up extended segment tables */
            area_$seg_table_t *src_seg_table = area_$lookup_seg_table(
                src_entry->owner_asid, area_id, byte_idx >> 8);

            if (src_seg_table == NULL) {
                CRASH_SYSTEM(&Area_Internal_Error);
            }

            int16_t table_offset = M$OIS$WLW(byte_idx - 2, 0x100) * 4;
            src_bitmap_ptr = (uint8_t *)src_seg_table->bitmap_ptr + table_offset;

            area_$seg_table_t *dst_seg_table = area_$lookup_seg_table(
                dst_entry->owner_asid, new_area_id, byte_idx >> 8);

            if (dst_seg_table == NULL) {
                CRASH_SYSTEM(&Area_Internal_Error);
            }

            dst_bitmap_ptr = (uint8_t *)dst_seg_table->bitmap_ptr + table_offset;
        }

        /* Process each bit in the byte */
        for (int bit = 0; bit < 8; bit++) {
            uint8_t bitmap_byte = *src_bitmap_ptr;

            if ((bitmap_byte & (1 << bit)) != 0) {
                /* This segment is allocated */

                /* Skip if in stack region */
                if (seg_page >= stack_low_page && seg_page < stack_high_page) {
                    seg_counter++;
                    seg_page += seg_direction;
                    continue;
                }

                /* Get ASTE for source and destination */
                void *src_aste;
                void *dst_aste;

                ML_$LOCK(ML_LOCK_AST);

                src_aste = area_$get_aste(area_id, src_bitmap_ptr,
                                           seg_counter, 0, (int8_t)-1, &status);
                if (status != status_$ok) {
                    CRASH_SYSTEM(&Area_Internal_Error);
                }

                dst_aste = area_$get_aste(new_area_id, dst_bitmap_ptr,
                                           seg_counter, 0, (int8_t)-1, &status);
                if (status != status_$ok) {
                    CRASH_SYSTEM(&Area_Internal_Error);
                }

                ML_$UNLOCK(ML_LOCK_AST);

                /* Copy the segment through AST */
                AST_$COPY_AREA(area_id, param_4, src_aste, dst_aste,
                               seg_counter, (uint32_t)seg_page << 15, &status);

                /* Decrement reference counts */
                *(int8_t *)((char *)src_aste + 0x11) -= 1;
                *(int8_t *)((char *)dst_aste + 0x11) -= 1;

                if (status != status_$ok) {
                    /* Clean up on failure */
                    area_$internal_delete(dst_entry, new_area_id, &temp_status, (int8_t)-1);
                    goto finish;
                }
            }

            seg_counter++;
            seg_page += seg_direction;
        }
    }

finish:
    /* Clear in-transition flag on source */
    src_entry->flags &= ~AREA_FLAG_IN_TRANS;
    EC_$ADVANCE(&AREA_$IN_TRANS_EC);

    return new_handle;
}
