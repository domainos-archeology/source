/*
 * tpad/set_mode.c - TPAD_$SET_MODE and TPAD_$SET_UNIT_MODE implementation
 *
 * Sets mode, scale factors, and hysteresis for the pointing device.
 *
 * Original addresses: 0x00E697BE, 0x00E697F0
 */

#include "tpad/tpad_internal.h"

/*
 * TPAD_$SET_MODE - Set pointing device mode
 *
 * Sets the operating mode and scaling parameters for the current unit.
 * This routine is user callable.
 */
void TPAD_$SET_MODE(tpad_$mode_t *new_modep, int16_t *xsp, int16_t *ysp,
                    int16_t *hysteresisp, smd_$pos_t *originp)
{
    status_$t status;

    TPAD_$SET_UNIT_MODE(&tpad_$unit, new_modep, xsp, ysp, hysteresisp, originp, &status);
}

/*
 * TPAD_$SET_UNIT_MODE - Set mode for specific unit
 *
 * Sets operating parameters for a specific display unit.
 */
void TPAD_$SET_UNIT_MODE(int16_t *unitp, tpad_$mode_t *new_modep, int16_t *xsp,
                         int16_t *ysp, int16_t *hysteresisp, smd_$pos_t *originp,
                         status_$t *status_ret)
{
    tpad_$unit_config_t *config;
    int16_t ndevices;

    /* Validate unit number */
    if (*unitp <= 0) {
        *status_ret = status_$display_invalid_unit_number;
        return;
    }

    ndevices = SMD_$N_DEVICES();
    if (*unitp > ndevices) {
        *status_ret = status_$display_invalid_unit_number;
        return;
    }

    /* Get unit configuration */
    config = TPAD_$UNIT_CONFIG(*unitp);

    /* Set mode and scale factors */
    config->mode = *(int16_t *)new_modep;
    config->x_scale = *xsp;
    config->y_scale = *ysp;

    /* Compute X conversion factor */
    if (config->x_scale == 0) {
        config->x_factor = TPAD_$FACTOR_DEFAULT;
    } else {
        config->x_factor = config->x_range / config->x_scale;
    }

    /* Compute Y conversion factor */
    if (config->y_scale == 0) {
        config->y_factor = TPAD_$FACTOR_DEFAULT;
    } else {
        config->y_factor = config->y_range / config->y_scale;
    }

    /* Set hysteresis */
    config->hysteresis = *hysteresisp;

    /* In absolute mode, set origin and cursor offset */
    if (config->mode == tpad_$absolute) {
        config->origin = *originp;
        config->cursor_offset_y = originp->y;
        config->cursor_offset_x = originp->x;
    }

    *status_ret = status_$ok;
}
