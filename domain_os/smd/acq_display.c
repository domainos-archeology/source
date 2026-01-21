/*
 * smd/acq_display.c - SMD_$ACQ_DISPLAY implementation
 *
 * Acquire display for exclusive access.
 *
 * Original address: 0x00E6EB42
 *
 * This function acquires a lock on the display for the calling process.
 * It handles various lock states and waits if the display is busy.
 * The lock state machine allows for different types of operations
 * (regular lock, scroll in progress, etc.).
 *
 * Assembly (key parts):
 *   00e6eb42    link.w A6,-0x14
 *   00e6eb46    movem.l {  A5 A4 A3 A2 D3 D2},-(SP)
 *   00e6eb4a    lea (0xe82b8c).l,A5           ; A5 = globals base
 *   00e6eb50    move.w (0x00e2060a).l,D0w     ; D0 = PROC1_$AS_ID
 *   00e6eb56    movea.l (0x8,A6),A2           ; A2 = lock_data param
 *   00e6eb5a    add.w D0w,D0w                 ; D0 *= 2
 *   00e6eb5c    move.w (0x48,A5,D0w*0x1),D0w  ; D0 = asid_to_unit[ASID]
 *   00e6eb60    movea.l #0xe2e3fc,A0          ; A0 = display units base
 *   ; ... computes unit offset and gets hw pointer ...
 *   00e6eb74    move.w (0x22,A3),D2w          ; D2 = hw->video_flags
 *   ; ... main loop starts ...
 *   00e6ebf2    pea (A2)
 *   00e6ebf4    pea (A3)
 *   00e6ebf6    jsr 0x00e15cce.l              ; SMD_$LOCK_DISPLAY
 *   ; ... handles lock states and EC_$WAIT ...
 */

#include "smd/smd_internal.h"

/* Display units base address */
#define SMD_DISPLAY_UNITS_BASE  0x00E2E3FC
#define SMD_DISPLAY_UNIT_SIZE   0x10C

/*
 * SMD_$ACQ_DISPLAY - Acquire display lock
 *
 * Acquires exclusive access to the display for the calling process.
 * Blocks if another process holds the lock, unless interrupted.
 *
 * The function loops calling SMD_$LOCK_DISPLAY until the lock is acquired.
 * If the lock returns with a special state (1-5, 7), it waits on the
 * lock eventcount before retrying. State 6 or higher clears the state.
 *
 * Parameters:
 *   lock_data - Pointer to lock-specific data (passed to SMD_$LOCK_DISPLAY)
 *
 * Returns:
 *   The video_flags value from the display hardware info
 *
 * Lock states:
 *   0 - Unlocked
 *   1 - Regular lock
 *   2 - Scroll operation setup
 *   3 - Scroll complete
 *   4 - Scroll cleanup
 *   5 - Initial lock
 *   6+ - Clear state (unlock)
 */
uint16_t SMD_$ACQ_DISPLAY(int16_t *lock_data)
{
    smd_display_hw_t *hw;
    smd_display_unit_t *unit_ptr;
    uint16_t asid;
    uint16_t unit_num;
    uint16_t video_flags;
    int8_t lock_result;
    int16_t wait_result;
    uint16_t lock_state;

    /* Get current process's display unit */
    asid = PROC1_$AS_ID;
    unit_num = SMD_GLOBALS.asid_to_unit[asid];

    /* Calculate display unit structure address
     * Base + unit_num * 0x10C, then access -0xF4 for hw pointer */
    unit_ptr = smd_get_unit(unit_num);
    hw = unit_ptr->hw;

    /* Save video flags to return */
    video_flags = hw->video_flags;

    /* Main lock acquisition loop */
    for (;;) {
        /* Try to acquire the lock */
        lock_result = SMD_$LOCK_DISPLAY(hw, lock_data);

        /* If lock failed (negative result), return current video flags */
        if (lock_result < 0) {
            return video_flags;
        }

        /* If lock_data[0] == 1, set the field_20 flag */
        if (*lock_data == 1) {
            hw->field_20 = 0xFF;
        }

        /* Handle different lock states */
        lock_state = hw->lock_state;

        switch (lock_state) {
            case 1:  /* Regular lock */
            case 2:  /* Scroll setup */
            case 3:  /* Scroll complete */
            case 4:  /* Scroll cleanup */
            case 5:  /* Initial lock */
            case 7:  /* Alternate state */
                /* Wait on the lock eventcount for the lock to be released */
                /* EC_$WAIT signature: ec_array, value_array */
                /* We wait for lock_ec value to change */
                wait_result = EC_$WAIT_1(&hw->lock_ec, hw->lock_ec.count + 1,
                                          &TIME_$CLOCKH, 0);

                if (wait_result != 0) {
                    /* Wait was interrupted or timed out, clear state */
                    goto clear_state;
                }
                break;

            case 6:  /* Clear state */
            default:
clear_state:
                hw->lock_state = SMD_LOCK_STATE_UNLOCKED;
                break;
        }

        /* Clear the field_20 flag before next iteration */
        hw->field_20 = 0;
    }
}
