/*
 * smd/set_clip_window.c - SMD_$SET_CLIP_WINDOW implementation
 *
 * Sets the clipping window for drawing operations.
 *
 * Original address: 0x00E6FE30
 *
 * Assembly analysis shows this function:
 * 1. Gets the current process's display unit from SMD_GLOBALS.asid_to_unit
 * 2. Computes offset into SMD_DISPLAY_INFO table (unit * 0x60)
 * 3. Copies the clip window from params to the display info entry
 * 4. Clamps the window to the display bounds
 *
 * Display info structure offsets (from 0xE27376 + unit*0x60):
 *   -0x12 (0x04): clip_x1_default (min x)
 *   -0x10 (0x06): clip_y1_default (max x)
 *   -0x0E (0x08): clip_x2_default (min y)
 *   -0x0C (0x0A): clip_y2_default (max y)
 *   -0x0A (0x0C): clip_x1 (current)
 *   -0x08 (0x0E): clip_y1 (current)
 *   -0x06 (0x10): clip_x2 (current)
 *   -0x04 (0x12): clip_y2 (current)
 */

#include "smd/smd_internal.h"

/*
 * SMD_$SET_CLIP_WINDOW - Set clipping window
 *
 * Sets the clipping window for the current process's display.
 * The window is clamped to the display bounds.
 *
 * Parameters:
 *   clip_rect  - Pointer to clip window (x1, y1, x2, y2)
 *   status_ret - Output: status return
 *
 * Returns:
 *   status_$ok on success
 *   status_$display_invalid_use_of_driver_procedure if no display associated
 */
void SMD_$SET_CLIP_WINDOW(int16_t *clip_rect, status_$t *status_ret)
{
    uint16_t unit;
    int32_t offset;
    smd_display_info_t *info;

    /* Get current process's display unit */
    unit = SMD_GLOBALS.asid_to_unit[PROC1_$AS_ID];

    if (unit == 0) {
        *status_ret = status_$display_invalid_use_of_driver_procedure;
        return;
    }

    *status_ret = status_$ok;

    /* Calculate offset into display info table: unit * 0x60 */
    offset = (int32_t)unit * SMD_DISPLAY_INFO_SIZE;
    info = (smd_display_info_t *)((uint8_t *)SMD_DISPLAY_INFO + offset);

    /* Copy clip window from params */
    info->clip_x1 = clip_rect[0];
    info->clip_y1 = clip_rect[1];
    info->clip_x2 = clip_rect[2];
    info->clip_y2 = clip_rect[3];

    /* Clamp x1 to display minimum */
    if (clip_rect[0] < info->clip_x1_default) {
        info->clip_x1 = info->clip_x1_default;
    }

    /* Clamp y1 to display minimum */
    if (clip_rect[2] < info->clip_x2_default) {
        info->clip_x2 = info->clip_x2_default;
    }

    /* Clamp x2 to display maximum (stored at offset for clip_y1_default) */
    if (info->clip_y1_default < clip_rect[1]) {
        info->clip_y1 = info->clip_y1_default;
    }

    /* Clamp y2 to display maximum */
    if (info->clip_y2_default < clip_rect[3]) {
        info->clip_y2 = info->clip_y2_default;
    }
}
