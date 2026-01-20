/*
 * tpad/punch_impact.c - TPAD_$SET_PUNCH_IMPACT and TPAD_$INQ_PUNCH_IMPACT
 *
 * Edge impact threshold management for stylus punch detection.
 *
 * Original addresses: 0x00E69ADC, 0x00E69B2E
 */

#include "tpad/tpad_internal.h"

/*
 * TPAD_$SET_PUNCH_IMPACT - Set edge impact threshold
 *
 * Sets the threshold for detecting edge impacts (stylus punches).
 * Returns the previous threshold value.
 */
int16_t TPAD_$SET_PUNCH_IMPACT(int16_t *unitp, int16_t *impact, status_$t *status_ret)
{
    tpad_$unit_config_t *config;
    int16_t ndevices;
    int16_t old_impact;

    /* Validate unit number */
    if (*unitp <= 0) {
        *status_ret = status_$display_invalid_unit_number;
        return 0;
    }

    ndevices = SMD_$N_DEVICES();
    if (*unitp > ndevices) {
        *status_ret = status_$display_invalid_unit_number;
        return 0;
    }

    /* Get unit configuration */
    config = TPAD_$UNIT_CONFIG(*unitp);

    /* Save old value and set new value */
    old_impact = config->punch_impact;
    config->punch_impact = *impact;

    *status_ret = status_$ok;
    return old_impact;
}

/*
 * TPAD_$INQ_PUNCH_IMPACT - Inquire edge impact threshold
 *
 * Returns the current edge impact threshold for a display unit.
 */
void TPAD_$INQ_PUNCH_IMPACT(int16_t *unitp, int16_t *impact, status_$t *status_ret)
{
    tpad_$unit_config_t *config;
    int16_t ndevices;

    /* Validate unit number */
    if (*unitp <= 0) {
        *impact = 0;
        *status_ret = status_$display_invalid_unit_number;
        return;
    }

    ndevices = SMD_$N_DEVICES();
    if (*unitp > ndevices) {
        *impact = 0;
        *status_ret = status_$display_invalid_unit_number;
        return;
    }

    /* Get unit configuration */
    config = TPAD_$UNIT_CONFIG(*unitp);

    *impact = config->punch_impact;
    *status_ret = status_$ok;
}
