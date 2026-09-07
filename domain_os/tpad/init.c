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
        SMD_$INQ_DISP_INFO((uint16_t *)&unit, &disp_info, &status);

        /*
         * If display info is valid (type != 0), use display dimensions.
         * 0x00E335C6 "move.w (-0x8,A6),D0w" / "subq.w #0x1" /
         * "move.w D0w,(-0x14,A3)" takes the result record's +0x08 field - the
         * *height* now that smd_disp_info_result_t is named correctly (bead
         * source-5nq5) - into config +0x18 (y_max_disp), and 0x00E335D0 takes
         * the +0x06 field (the width) into config +0x14 (x_max_disp).
         */
        if (disp_info.display_type != 0) {
            config->y_max_disp = disp_info.height - 1;
            config->x_max_disp = disp_info.width - 1;
        }

        /*
         * Resolved (bead source-j999): the header's names for +0x06/+0x08
         * (x_scale/y_scale) and +0x0A/+0x0C (x_range/y_range) are correct and
         * this file was inverted.  TPAD_$SET_UNIT_MODE writes its `xs`/`ys`
         * arguments to +0x06/+0x08 (0x00E69838 "move.w (A4),(-0x26,A1)" and
         * 0x00E69840 "move.w (A0),(-0x24,A1)", A1 = config + 0x2C), and
         * TPAD_$RE_RANGE_UNIT seeds +0x0A/+0x0C with TPAD_$INITIAL_RANGE
         * (0x00E69A74/0x00E69A7A "move.w #0x200,(-0x22,A1)"/"(-0x20,A1)")
         * beside x_min/y_min.  So +0x06/+0x08 is the scale pair and
         * +0x0A/+0x0C the range pair.
         *
         * TPAD_$INIT therefore copies the display bounds into the *scale*
         * fields:
         *   0x00E335DA move.w (-0x18,A3),(-0x26,A3)  ->  +0x06 = +0x14
         *   0x00E335E0 move.w (-0x14,A3),(-0x24,A3)  ->  +0x08 = +0x18
         * (A3 = config + 0x2C throughout the loop, so displacement -0x2C+k is
         * config offset k.)
         */
        config->x_scale = config->x_max_disp;
        config->y_scale = config->y_max_disp;

        /*
         * Conversion factors, identical in all three writers of these fields:
         *   0x00E335E6 tst.w (-0x26,A3)      -> if x_scale == 0
         *   0x00E335EC move.w #0x400,(-0x10,A3)      x_factor = 0x400
         *   0x00E335F4 move.w (-0x22,A3),D0w / ext.l / divs.w (-0x26,A3),D0
         *   0x00E335FE move.w D0w,(-0x10,A3)         x_factor = x_range/x_scale
         * and the same shape at 0x00E33602-0x00E3361A for Y using +0x08,
         * +0x0C and +0x1E.  Compare TPAD_$RE_RANGE_UNIT 0x00E69A80-0x00E69AB4
         * and TPAD_$SET_UNIT_MODE 0x00E69844-0x00E69878, which are byte for
         * byte the same sequence.
         */
        if (config->x_scale == 0) {
            config->x_factor = TPAD_$FACTOR_DEFAULT;  /* 0x400 = 1024 */
        } else {
            config->x_factor = config->x_range / config->x_scale;
        }

        if (config->y_scale == 0) {
            config->y_factor = TPAD_$FACTOR_DEFAULT;  /* 0x400 = 1024 */
        } else {
            config->y_factor = config->y_range / config->y_scale;
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
