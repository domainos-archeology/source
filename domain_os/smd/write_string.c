/*
 * smd/write_string.c - Write string to display
 *
 * High-level string output function that temporarily sets the clip
 * window to the default bounds before rendering text. This allows
 * text to be drawn without the current clip window restrictions.
 *
 * Original address: 0x00E6FEBE
 */

#include "smd/smd_internal.h"

/*
 * SMD_$WRITE_STRING - Write string to display
 *
 * Renders a string at the specified position using the given font.
 * Temporarily sets the clip window to the default bounds for the
 * display, then restores the original clip window after rendering.
 *
 * Parameters:
 *   pos        - Pointer to position (x, y packed as uint32)
 *   font       - Font to use for rendering
 *   buffer     - Text buffer to render
 *   length     - Pointer to string length
 *   param5     - Additional rendering parameters (flags)
 *   status_ret - Status return
 *
 * Status codes:
 *   status_$ok - Success
 *   status_$display_invalid_use_of_driver_procedure - No display associated
 *
 * Notes:
 *   - Position format: high 16 bits = y, low 16 bits = x
 *   - Clip window temporarily set to default bounds (offset -0x12 from info)
 *   - Original clip window restored after rendering
 */
void SMD_$WRITE_STRING(uint32_t *pos, void *font, void *buffer,
                       uint16_t *length, void *param5, status_$t *status_ret)
{
    uint16_t unit;
    uint16_t asid;
    uint32_t local_pos;
    uint16_t local_length;
    smd_display_info_t *info;
    int32_t saved_clip_1;   /* Saved clip_x1/y1 (combined) */
    int32_t saved_clip_2;   /* Saved clip_x2/y2 (combined) */

    /* Copy parameters to local storage */
    local_pos = *pos;
    local_length = *length;

    /* Get current process's ASID */
    asid = PROC1_$AS_ID;

    /* Look up display unit for this ASID */
    unit = SMD_GLOBALS.asid_to_unit[asid];
    if (unit == 0) {
        *status_ret = status_$display_invalid_use_of_driver_procedure;
        return;
    }

    /*
     * Get display info pointer.
     * Info table base is 0x00E27376, entry size is 0x60 bytes.
     * The clip window is at offsets 0x14-0x1B (current) and 0x0C-0x13 (default).
     * Address calculation: base + unit * 0x60
     *
     * Original code accesses via (base - 0x0A + unit * 0x60) for clip region.
     */
    info = &SMD_DISPLAY_INFO[unit];

    /*
     * Save current clip window (offsets 0x14-0x1B).
     * Accessed as two 32-bit values at -0x0A and -0x06 from struct base + 0x60.
     * This corresponds to clip_x1/y1 and clip_x2/y2.
     */
    saved_clip_1 = *(int32_t *)&info->clip_x1;
    saved_clip_2 = *(int32_t *)&info->clip_x2;

    /*
     * Set clip window to default bounds (offsets 0x0C-0x13).
     * Copy default clip region to current clip region.
     */
    *(int32_t *)&info->clip_x1 = *(int32_t *)&info->clip_x1_default;
    *(int32_t *)&info->clip_x2 = *(int32_t *)&info->clip_x2_default;

    /* Call internal string rendering function */
    SMD_$WRITE_STR_CLIP(&local_pos, font, buffer, &local_length, param5, status_ret);

    /* Restore original clip window */
    *(int32_t *)&info->clip_x1 = saved_clip_1;
    *(int32_t *)&info->clip_x2 = saved_clip_2;
}
