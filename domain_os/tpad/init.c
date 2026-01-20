/*
 * tpad/init.c - TPAD_$INIT implementation
 *
 * Initialize the pointing device subsystem.
 *
 * Original address: 0x00E33570
 */

#include "tpad/tpad_internal.h"

/*
 * TPAD_$INIT - Initialize the pointing device subsystem
 *
 * Initializes all per-unit configurations based on display dimensions
 * and sets default global state. Called during system startup.
 *
 * For each display unit:
 *   1. Queries display info to get screen dimensions
 *   2. Initializes display boundary values (x_max_disp, y_max_disp)
 *   3. Sets coordinate range to match display dimensions
 *   4. Computes conversion factors
 *
 * Also initializes global state:
 *   - Sets default cursor position (512, 400)
 *   - Sets default touchpad max coordinate (1500)
 *   - Clears device type to unknown
 *   - Records initial clock time
 */
void TPAD_$INIT(void)
{
    int16_t unit;
    int16_t ndevices;
    tpad_$unit_config_t *config;
    smd_disp_info_result_t disp_info;
    status_$t status;

    /* Set initial unit for TPAD operations */
    TPAD_$SET_UNIT(&tpad_$unit_num_for_init);

    /* Get number of display devices */
    ndevices = SMD_$N_DEVICES();

    /* Initialize per-unit configurations */
    for (unit = 1; unit <= ndevices; unit++) {
        config = TPAD_$UNIT_CONFIG(unit);

        /* Query display dimensions */
        SMD_$INQ_DISP_INFO(&unit, &disp_info, &status);

        /* If display info is valid (type != 0), use display dimensions */
        if (disp_info.display_type != 0) {
            config->y_max_disp = disp_info.height - 1;
            config->x_max_disp = disp_info.width - 1;
        }

        /* Copy display bounds to coordinate range */
        config->x_range = config->x_max_disp;
        config->y_range = config->y_max_disp;

        /* Compute X conversion factor */
        if (config->x_range == 0) {
            config->x_factor = TPAD_$FACTOR_DEFAULT;  /* 0x400 = 1024 */
        } else {
            config->x_factor = config->x_scale / config->x_range;
        }

        /* Compute Y conversion factor */
        if (config->y_range == 0) {
            config->y_factor = TPAD_$FACTOR_DEFAULT;  /* 0x400 = 1024 */
        } else {
            config->y_factor = config->y_scale / config->y_range;
        }
    }

    /* Initialize global state */

    /* Get initial clock timestamp */
    TIME_$CLOCK(&tpad_$last_clock);

    /* Set default cursor position */
    tpad_$cursor_y = TPAD_$DEFAULT_CURSOR_Y;  /* 0x200 = 512 */
    tpad_$cursor_x = TPAD_$DEFAULT_CURSOR_X;  /* 0x190 = 400 */

    /* Clear button state */
    tpad_$button_state = 0;

    /* Clear accumulated movement */
    tpad_$accum_x = 0;
    tpad_$accum_y = 0;

    /* Set default touchpad maximum coordinate */
    tpad_$touchpad_max = TPAD_$DEFAULT_TOUCHPAD_MAX;  /* 0x5dc = 1500 */

    /* Clear device type */
    tpad_$dev_type = tpad_$unknown;
}
