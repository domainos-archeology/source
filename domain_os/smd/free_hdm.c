/*
 * smd/free_hdm.c - Free hidden display memory
 *
 * Frees a previously allocated region of off-screen display memory (HDM).
 * Handles merging of adjacent free blocks to prevent fragmentation.
 *
 * Original address: 0x00E6DA3A
 */

#include "smd/smd_internal.h"

/*
 * SMD_$FREE_HDM - Free hidden display memory
 *
 * Frees a region of off-screen display memory back to the free list.
 * Adjacent free blocks are merged to reduce fragmentation.
 *
 * Parameters:
 *   pos        - Input: position to free (x, y from previous alloc)
 *   size_ptr   - Input: pointer to size being freed
 *   status_ret - Output: status return
 *
 * Status codes:
 *   status_$ok - Success
 *   status_$display_invalid_use_of_driver_procedure - No display associated
 *   status_$display_invalid_position_argument - Invalid position coordinates
 *   status_$display_error_unloading_internal_table - Corrupted free list
 *
 * Original implementation notes:
 *   - Validates position based on display type
 *   - Converts position back to linear offset
 *   - Finds insertion point in sorted free list
 *   - Merges with previous/next blocks if adjacent
 *   - Creates new entry if no merge possible
 */
void SMD_$FREE_HDM(smd_hdm_pos_t *pos, uint16_t *size_ptr, status_$t *status_ret)
{
    uint16_t size;
    uint16_t unit;
    uint16_t asid;
    uint8_t *unit_base;
    smd_hdm_list_t *hdm_list;
    smd_display_hw_t *hw;
    int16_t offset;         /* Linear offset of block being freed */
    int16_t insert_idx;     /* Index where to insert/merge */
    int16_t prev_end;       /* End offset of previous block */
    int16_t next_start;     /* Start offset of next block */
    int16_t block_end;      /* End of block being freed */
    int i;

    size = *size_ptr;

    /* Get current process's ASID */
    asid = PROC1_$AS_ID;

    /* Look up display unit for this ASID */
    unit = SMD_GLOBALS.asid_to_unit[asid];
    if (unit == 0) {
        *status_ret = status_$display_invalid_use_of_driver_procedure;
        return;
    }

    /* Calculate unit base and get HDM list pointer */
    unit_base = ((uint8_t *)SMD_DISPLAY_UNITS) + (uint32_t)unit * SMD_DISPLAY_UNIT_SIZE;
    hdm_list = *(smd_hdm_list_t **)(unit_base + 0x04);

    /* Get hw pointer for display type */
    hw = *(smd_display_hw_t **)(unit_base - 0xf4);

    /*
     * Validate position and convert to linear offset based on display type.
     */
    if (hw->display_type == SMD_DISP_TYPE_MONO_LANDSCAPE) {
        /* Display type 1: mono landscape 1024x800 */
        /* Valid: x in [0x31, 0x3ff], y must be 800 */
        if (pos->x > 0x3ff || pos->y != 800 || pos->x < 0x31) {
            *status_ret = status_$display_invalid_position_argument;
            return;
        }
        offset = pos->x;
    } else if (hw->display_type == SMD_DISP_TYPE_MONO_PORTRAIT) {
        /* Display type 2: mono portrait 800x1024 */
        /* Reverse the allocation formula */
        offset = pos->x + pos->y - 800;

        /* Validate: y must be multiple of 0xe0, x >= 800 */
        if ((pos->y % 0xe0) != 0 || pos->x < 800 ||
            offset < 0 || offset > 0x3d7) {
            *status_ret = status_$display_invalid_position_argument;
            return;
        }
    } else {
        /* Unknown display type - use raw x as offset for now */
        /* TODO: Handle other display types */
        offset = pos->x;
    }

    /*
     * Find insertion point in the sorted free list.
     * Blocks are kept sorted by offset.
     */
    insert_idx = 1;  /* Start at first block (index 0 is after header) */
    while (insert_idx <= hdm_list->count) {
        next_start = hdm_list->blocks[insert_idx - 1].offset;
        if (offset < next_start) {
            break;
        }
        insert_idx++;
    }

    /*
     * Calculate previous block's end offset (if any).
     * If insert_idx > 1, there's a previous block.
     */
    if (insert_idx < 2) {
        prev_end = -1;  /* No previous block */
    } else {
        prev_end = hdm_list->blocks[insert_idx - 2].offset +
                   hdm_list->blocks[insert_idx - 2].size;
    }

    /*
     * Get next block's start offset (or sentinel value if at end).
     */
    if (insert_idx > hdm_list->count) {
        next_start = 0x401;  /* Past end sentinel */
    } else {
        next_start = hdm_list->blocks[insert_idx - 1].offset;
    }

    block_end = offset + size;

    /*
     * Validate: block being freed must not overlap existing free blocks.
     */
    if (block_end > next_start || offset < prev_end) {
        *status_ret = status_$display_error_unloading_internal_table;
        return;
    }

    /*
     * Handle merge cases:
     * 1. Merge with previous only (offset == prev_end)
     * 2. Merge with next only (block_end == next_start)
     * 3. Merge with both (combine into single block)
     * 4. No merge (insert new block)
     */

    if (offset == prev_end) {
        /* Can merge with previous block */
        if (block_end == next_start) {
            /*
             * Case 3: Merge with both previous and next.
             * Extend previous block to include this and next.
             * Remove the next block from list.
             */
            hdm_list->blocks[insert_idx - 2].size +=
                size + hdm_list->blocks[insert_idx - 1].size;
            hdm_list->count--;

            /* Shift remaining blocks down */
            for (i = insert_idx - 1; i < hdm_list->count; i++) {
                hdm_list->blocks[i] = hdm_list->blocks[i + 1];
            }
        } else {
            /*
             * Case 1: Merge with previous only.
             * Extend previous block's size.
             */
            hdm_list->blocks[insert_idx - 2].size += size;
        }
    } else if (block_end == next_start) {
        /*
         * Case 2: Merge with next only.
         * Extend next block backward (update offset and size).
         */
        hdm_list->blocks[insert_idx - 1].offset = offset;
        hdm_list->blocks[insert_idx - 1].size += size;
    } else {
        /*
         * Case 4: No merge possible - insert new block.
         * Check if there's room in the list.
         */
        if (hdm_list->count >= SMD_HDM_MAX_ENTRIES) {
            *status_ret = status_$display_error_unloading_internal_table;
            return;
        }

        /* Shift blocks up to make room */
        for (i = hdm_list->count; i >= insert_idx; i--) {
            hdm_list->blocks[i] = hdm_list->blocks[i - 1];
        }

        /* Insert new block */
        hdm_list->count++;
        hdm_list->blocks[insert_idx - 1].offset = offset;
        hdm_list->blocks[insert_idx - 1].size = size;
    }

    *status_ret = status_$ok;
}
