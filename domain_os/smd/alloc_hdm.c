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
 *   - For display type 1 (mono landscape): y=800, x=block offset
 *   - For display type 2 (mono portrait): coordinates calculated differently
 *   - If exact fit, removes block from list
 *   - If larger, splits block and updates remaining
 */
void SMD_$ALLOC_HDM(uint16_t *size_ptr, smd_hdm_pos_t *pos, status_$t *status_ret)
{
    uint16_t size;
    uint16_t unit;
    uint16_t asid;
    uint8_t *unit_base;
    smd_hdm_list_t *hdm_list;
    smd_display_hw_t *hw;
    int16_t num_blocks;
    int16_t i;
    int16_t block_offset;
    int16_t block_size;

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

    /* Search free list for a block large enough */
    num_blocks = hdm_list->count;
    for (i = 0; i < num_blocks; i++) {
        block_size = hdm_list->blocks[i].size;

        if (size <= block_size) {
            /* Found a suitable block */
            block_offset = hdm_list->blocks[i].offset;

            /*
             * Calculate output position based on display type.
             * Display type 1: mono landscape 1024x800
             *   - y = 800 (start of hidden memory below visible)
             *   - x = block offset
             *
             * Display type 2: mono portrait 800x1024
             *   - Different calculation involving division by 0xe0 (224)
             *   - y = (offset / 224) * 224
             *   - x = (offset % 224) + 800
             */
            if (hw->display_type == SMD_DISP_TYPE_MONO_LANDSCAPE) {
                pos->y = 800;
                pos->x = block_offset;
            } else if (hw->display_type == SMD_DISP_TYPE_MONO_PORTRAIT) {
                int16_t div_result = block_offset / 0xe0;
                pos->y = div_result * 0xe0;
                pos->x = (block_offset % 0xe0) + 800;
            }
            /* TODO: Handle other display types if needed */

            if (size == block_size) {
                /*
                 * Exact fit - remove this block from the list.
                 * Shift remaining blocks down.
                 */
                hdm_list->count = num_blocks - 1;
                for (int j = i; j < hdm_list->count; j++) {
                    hdm_list->blocks[j] = hdm_list->blocks[j + 1];
                }
            } else {
                /*
                 * Partial allocation - update block to reflect remaining space.
                 * Reduce size and advance offset.
                 */
                hdm_list->blocks[i].size = block_size - size;
                hdm_list->blocks[i].offset = block_offset + size;
            }

            *status_ret = status_$ok;
            return;
        }
    }

    /* No suitable block found */
    *status_ret = status_$display_hidden_display_memory_full;
}
