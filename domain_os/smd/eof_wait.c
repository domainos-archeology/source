/*
 * smd/eof_wait.c - SMD_$EOF_WAIT implementation
 *
 * Waits for end-of-frame (display refresh complete) before continuing.
 * Used to synchronize with vertical blank for tear-free updates.
 *
 * Original address: 0x00E6F3AE
 */

#include "smd/smd_internal.h"
#include "ec/ec.h"

/* Status code for quit while waiting */
#define status_$display_quit_while_waiting  0x00130022

/*
 * SMD_$EOF_WAIT - Wait for end-of-frame
 *
 * Blocks the calling process until the next vertical blank / end-of-frame
 * signal from the display hardware. Used to synchronize display updates
 * with the refresh cycle for smooth, tear-free output.
 *
 * Can be interrupted by a quit signal (FIM quit event).
 *
 * Parameters:
 *   status_ret - Output: receives status code
 *                status_$ok on success
 *                status_$display_invalid_use_of_driver_procedure if no display
 *                status_$display_quit_while_waiting if quit signal received
 */
void SMD_$EOF_WAIT(status_$t *status_ret)
{
    uint16_t unit_idx;
    uint16_t saved_state;
    smd_display_unit_t *unit_ptr;
    smd_display_hw_t *hw;
    uint32_t target_count;
    ec_$eventcount_t *wait_ecs[3];
    uint32_t wait_values[3];
    int16_t wait_result;

    /* Get display unit for current process */
    unit_idx = SMD_GLOBALS.asid_to_unit[PROC1_$AS_ID];

    if (unit_idx == 0) {
        /* No display associated with this process */
        *status_ret = status_$display_invalid_use_of_driver_procedure;
        return;
    }

    /* Acquire display lock for the operation */
    saved_state = SMD_$ACQ_DISPLAY(&SMD_ACQ_LOCK_DATA);

    /* Get unit and hardware pointers */
    unit_ptr = &SMD_DISPLAY_UNITS[unit_idx];
    hw = unit_ptr->hw;

    /* Calculate target event count (current + 1) */
    target_count = hw->lock_ec.count + 1;

    /* Set lock state to indicate waiting for EOF */
    hw->lock_state = 7;  /* EOF wait state */

    /* Set up wait on: lock EC, quit EC, and null */
    /* The display hardware advances lock_ec on each frame */
    wait_ecs[0] = &hw->lock_ec;
    wait_ecs[1] = &FIM_$QUIT_EC[PROC1_$AS_ID];
    wait_ecs[2] = NULL;

    wait_values[0] = target_count;
    wait_values[1] = FIM_$QUIT_EC[PROC1_$AS_ID].count + 1;
    wait_values[2] = 0;

    /* Wait for either frame complete or quit signal */
    wait_result = EC_$WAIT(wait_ecs, wait_values);

    if (wait_result == 0) {
        /* Woke up on lock_ec - frame complete */
        *status_ret = status_$ok;
    } else {
        /* Woke up on quit_ec - quit signal received */
        *status_ret = status_$display_quit_while_waiting;

        /* Save quit value for later retrieval */
        FIM_$QUIT_VALUE[PROC1_$AS_ID] = FIM_$QUIT_EC[PROC1_$AS_ID].count;
    }

    /* Restore video state (from saved_state) */
    /* The second byte of hw+8 is the video state register address */
    *(uint16_t *)((uint8_t *)unit_ptr + 8) = saved_state;

    /* Release display lock */
    SMD_$REL_DISPLAY();
}
