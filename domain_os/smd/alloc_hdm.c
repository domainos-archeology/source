/*
 * smd/alloc_hdm.c - Allocate hidden display memory
 *
 * Allocates a region of off-screen display memory (HDM) for use as
 * a backing store for sprites, fonts, or temporary graphics operations.
 *
 * HDM is organized as a free list of contiguous blocks. Each block
 * has an offset (in scanlines) and a size (number of scanlines).
 *
 * Original address: 0x00E6D92E
 */

#include "smd/smd_internal.h"

/*
 * SMD_$ALLOC_HDM - Allocate hidden display memory
 *
 * Allocates a contiguous region of off-screen display memory.
 * The allocation is managed via a free list stored per display unit.
 *
 * Parameters:
 *   size_ptr   - Input: pointer to size to allocate (in scanlines)
 *   pos        - Output: position in HDM (x, y coordinates)
 *   status_ret - Output: status return
 *
 * Status codes:
 *   status_$ok - Success
 *   status_$display_invalid_use_of_driver_procedure - No display associated
 *   status_$display_hidden_display_memory_full - No space available
 *
 * Original implementation notes:
 *   - Searches free list for first-fit block
 *   - For display type 1 (mono portrait, 800x1024): x=800, y=block offset
 *   - For display type 2 (mono landscape, 1024x800): tiled into the 224-line
 *     hidden band starting at y=800
 *   - If exact fit, removes block from list
 *   - If larger, splits block and updates remaining
 */
void SMD_$ALLOC_HDM(uint16_t *size_ptr, smd_hdm_pos_t *pos, status_$t *status_ret)
{
    int16_t size;   /* word load + signed cmp.w/bgt at 0x00E6D992 */
    uint16_t unit;
    uint16_t asid;
    smd_hdm_list_t *hdm_list;
    smd_display_hw_t *hw;
    int16_t num_blocks;
    int16_t i;
    int16_t block_offset;
    int16_t block_size;

    size = (int16_t)*size_ptr;

    /* Get current process's ASID */
    asid = PROC1_$AS_ID;

    /* Look up display unit for this ASID */
    unit = SMD_GLOBALS.asid_to_unit[asid];
    if (unit == 0) {
        *status_ret = status_$display_invalid_use_of_driver_procedure;
        return;
    }

    /* 0x00e6d966-0x00e6d978: A0 = 0xE2E3FC + unit*0x10C; the free list is at
     * (0x4,A0) = record +0xF8 and the hardware record at (-0xF4,A0). */
    {
        smd_display_unit_t *rec = smd_$unit_rec((int16_t)unit);

        hdm_list = rec->hdm_list;
        hw = rec->hw;
    }

    /* 0x00e6d97e-0x00e6da24: walk the free list, first fit.  D3 = count-1
     * plus dbf makes exactly `count` iterations. */
    num_blocks = (int16_t)hdm_list->count;
    for (i = 0; i < num_blocks; i++) {
        block_size = hdm_list->blocks[i].size;

        if (size <= block_size) {
            /* Found a suitable block */
            block_offset = hdm_list->blocks[i].offset;

            /*
             * Calculate the output position from the display type
             * (0x00e6d998-0x00e6d9e2).
             */
            if (hw->display_type == SMD_DISP_TYPE_MONO_PORTRAIT) {
                /* 0x00e6d9ae/0x00e6d9b4: x = 800 (the first hidden column of
                 * an 800-pixel-wide display), y = the block's scan line. */
                pos->x = 800;
                pos->y = (uint16_t)block_offset;
            } else if (hw->display_type == SMD_DISP_TYPE_MONO_LANDSCAPE) {
                /* 0x00e6d9ba-0x00e6d9e2: the hidden band is the 224 scan lines
                 * from 800 up, tiled 224 columns at a time. */
                int16_t div_result = (int16_t)(block_offset / 0xe0);
                pos->x = (uint16_t)(div_result * 0xe0);
                pos->y = (uint16_t)((block_offset % 0xe0) + 800);
            }
            /* 0x00e6d9aa: any other display type leaves *pos untouched. */

            if (size == block_size) {
                /*
                 * Exact fit - remove this block from the list.
                 * Shift remaining blocks down.
                 */
                hdm_list->count = (uint16_t)(num_blocks - 1);
                for (int j = i; j < (int16_t)hdm_list->count; j++) {
                    hdm_list->blocks[j] = hdm_list->blocks[j + 1];
                }
            } else {
                /*
                 * Partial allocation - update block to reflect remaining space.
                 * Reduce size and advance offset.
                 */
                hdm_list->blocks[i].size = (uint16_t)(block_size - size);
                hdm_list->blocks[i].offset = (uint16_t)(block_offset + size);
            }

            *status_ret = status_$ok;
            return;
        }
    }

    /* No suitable block found */
    *status_ret = status_$display_hidden_display_memory_full;
}
