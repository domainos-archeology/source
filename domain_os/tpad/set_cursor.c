/*
 * tpad/set_cursor.c - TPAD_$SET_CURSOR and TPAD_$SET_UNIT_CURSOR implementation
 *
 * Provides feedback from DM to re-origin relative mode when DM sets cursor.
 *
 * Original addresses: 0x00E698A0, 0x00E698C2
 */

#include "tpad/tpad_internal.h"

/*
 * TPAD_$SET_CURSOR - Set cursor position
 *
 * Provides feedback from the display manager to re-origin relative mode
 * when the DM sets a new cursor position. Called by smd_$move_kbd_cursor.
 */
void TPAD_$SET_CURSOR(smd_$pos_t *new_crsr)
{
    status_$t status;

    TPAD_$SET_UNIT_CURSOR(&tpad_$unit, new_crsr, &status);
}

/*
 * TPAD_$SET_UNIT_CURSOR - Set cursor position for specific unit
 *
 * Sets the cursor position and updates tracking state for relative mode.
 */
void TPAD_$SET_UNIT_CURSOR(int16_t *unitp, smd_$pos_t *new_crsr, status_$t *status_ret)
{
    tpad_$unit_config_t *config;
    int16_t ndevices;

    /* Validate unit number */
    if (tpad_$unit <= 0) {
        *status_ret = status_$display_invalid_unit_number;
        return;
    }

    ndevices = SMD_$N_DEVICES();
    if (tpad_$unit > ndevices) {
        *status_ret = status_$display_invalid_unit_number;
        return;
    }

    /* Get unit configuration */
    config = TPAD_$UNIT_CONFIG(*unitp);

    /* Set current unit */
    tpad_$unit = *unitp;

    /* Clear accumulated movement */
    tpad_$accum_x = 0;
    tpad_$accum_y = 0;

    /* Set cursor position */
    tpad_$cursor_y = new_crsr->y;
    tpad_$cursor_x = new_crsr->x;

    /* In non-absolute mode, update cursor offsets for relative tracking */
    if (config->mode != tpad_$absolute) {
        config->cursor_offset_x = tpad_$cursor_x - tpad_$raw_x;
        config->cursor_offset_y = tpad_$cursor_y - tpad_$raw_y;
    }

    /* Note: Original doesn't set status_$ok on success path, but does return */
}
