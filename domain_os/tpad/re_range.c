/*
 * tpad/re_range.c - TPAD_$RE_RANGE and TPAD_$RE_RANGE_UNIT implementation
 *
 * Re-establish the touchpad raw data range over the next 1000 data points.
 *
 * Original addresses: 0x00E69A0E, 0x00E69A2C
 */

#include "tpad/tpad_internal.h"

/*
 * TPAD_$RE_RANGE - Re-establish touchpad coordinate range
 *
 * Initiates auto-ranging to recalibrate the touchpad coordinate range.
 * Also done at system boot.
 */
void TPAD_$RE_RANGE(void)
{
    status_$t status;

    TPAD_$RE_RANGE_UNIT(&tpad_$unit, &status);
}

/*
 * TPAD_$RE_RANGE_UNIT - Re-range for specific unit
 *
 * Resets the ranging state so that the next 1000 samples will be used
 * to determine the touchpad's coordinate range.
 */
void TPAD_$RE_RANGE_UNIT(int16_t *unitp, status_$t *status_ret)
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

    /* Reset sample counter to start re-ranging */
    config->sample_count = 0;

    /* Reset range tracking to initial values */
    config->x_min = TPAD_$INITIAL_MIN;     /* 0x100 */
    config->y_min = TPAD_$INITIAL_MIN;     /* 0x100 */
    config->x_range = TPAD_$INITIAL_RANGE; /* 0x200 */
    config->y_range = TPAD_$INITIAL_RANGE; /* 0x200 */

    /* Recompute conversion factors */
    if (config->x_scale == 0) {
        config->x_factor = TPAD_$FACTOR_DEFAULT;
    } else {
        config->x_factor = config->x_range / config->x_scale;
    }

    if (config->y_scale == 0) {
        config->y_factor = TPAD_$FACTOR_DEFAULT;
    } else {
        config->y_factor = config->y_range / config->y_scale;
    }

    *status_ret = status_$ok;
}
