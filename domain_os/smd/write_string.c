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
 *   - Clip window temporarily set to the display bounds at info +0x4E..+0x55
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
    int16_t saved_clip_x1;  /* (-0x8,A6) .. (-0x2,A6) */
    int16_t saved_clip_x2;
    int16_t saved_clip_y1;
    int16_t saved_clip_y2;

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
     * Bead source-fqne: 0x00E6FF08 "lea (0x0,A2,D1*0x1),A2" leaves A2 at
     * base + unit*0x60 and every subsequent displacement is negative, so the
     * entry addressed is base + unit*0x60 - 0x60 - the table is 1-based on the
     * unit number.  Use smd_$unit_info().
     */
    info = smd_$unit_info((int16_t)unit);

    /*
     * Save the current clip window.
     *   00e6ff0c  lea (-0xa,A2),A4           ; A4 = entry + 0x56
     *   00e6ff10  move.l (A4)+,(-0x8,A6)     ; clip_x1, clip_x2
     *   00e6ff14  move.l (A4)+,(-0x4,A6)     ; clip_y1, clip_y2
     * The original moves two longwords; copying the four words individually is
     * the same bytes and keeps the C endian-neutral.
     */
    saved_clip_x1 = info->clip_x1;
    saved_clip_x2 = info->clip_x2;
    saved_clip_y1 = info->clip_y1;
    saved_clip_y2 = info->clip_y2;

    /*
     * Widen the clip window to the display's own bounds.
     *   00e6ff18  lea (-0x12,A2),A4          ; A4 = entry + 0x4E
     *   00e6ff1c  move.l (A4)+,(-0xa,A2)     ; clip_x1/clip_x2 = min_x/max_x
     *   00e6ff20  move.l (A4)+,(-0x6,A2)     ; clip_y1/clip_y2 = min_y/max_y
     */
    info->clip_x1 = info->min_x;
    info->clip_x2 = info->max_x;
    info->clip_y1 = info->min_y;
    info->clip_y2 = info->max_y;

    /* Call internal string rendering function */
    SMD_$WRITE_STR_CLIP(&local_pos, font, buffer, &local_length, param5, status_ret);

    /* Restore the original clip window (0x00E6FF40-0x00E6FF48). */
    info->clip_x1 = saved_clip_x1;
    info->clip_x2 = saved_clip_x2;
    info->clip_y1 = saved_clip_y1;
    info->clip_y2 = saved_clip_y2;
}
