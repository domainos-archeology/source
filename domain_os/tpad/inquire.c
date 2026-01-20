/*
 * tpad/inquire.c - TPAD_$INQUIRE and TPAD_$INQUIRE_UNIT implementation
 *
 * Query current mode settings for pointing device.
 *
 * Original addresses: 0x00E6993A, 0x00E6996C
 */

#include "tpad/tpad_internal.h"

/*
 * TPAD_$INQUIRE - Inquire current mode settings
 *
 * Returns the current mode settings for the current unit so a program
 * can save and later restore them.
 */
void TPAD_$INQUIRE(tpad_$mode_t *cur_modep, int16_t *xsp, int16_t *ysp,
                   int16_t *hysteresisp, smd_$pos_t *originp)
{
    status_$t status;

    TPAD_$INQUIRE_UNIT(&tpad_$unit, cur_modep, xsp, ysp,
                       hysteresisp, originp, &status);
}

/*
 * TPAD_$INQUIRE_UNIT - Inquire mode settings for specific unit
 *
 * Returns mode settings for a specified display unit.
 */
void TPAD_$INQUIRE_UNIT(int16_t *unitp, tpad_$mode_t *cur_modep, int16_t *xsp,
                        int16_t *ysp, int16_t *hysteresisp, smd_$pos_t *originp,
                        status_$t *status_ret)
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

    /* Return current settings */
    *cur_modep = (tpad_$mode_t)config->mode;
    *xsp = config->x_scale;
    *ysp = config->y_scale;
    *hysteresisp = config->hysteresis;
    *originp = config->origin;

    *status_ret = status_$ok;
}
