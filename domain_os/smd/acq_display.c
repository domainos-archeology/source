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
 *   00e6eb66    move.w D0w,D1w
 *   00e6eb68    mulu.w #0x10c,D1              ; D1 = unit * 0x10C
 *   00e6eb6c    lea (0x0,A0,D1*0x1),A0        ; A0 = biased unit record
 *   00e6eb70    movea.l (-0xf4,A0),A3         ; A3 = rec->hw
 *   00e6eb74    move.w (0x22,A3),D2w          ; D2 = hw->video_flags
 *   ; ... main loop starts ...
 *   00e6ebf2    pea (A2)
 *   00e6ebf4    pea (A3)
 *   00e6ebf6    jsr 0x00e15cce.l              ; SMD_$LOCK_DISPLAY
 *   ; ... handles lock states and EC_$WAIT ...
 */

#include "smd/smd_internal.h"

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
    uint16_t asid;
    uint16_t unit_num;
    uint16_t video_flags;
    int8_t lock_result;
    int32_t lock_ec_target;
    int32_t clock_target;
    int16_t wait_result;
    uint16_t lock_state;

    /* Get current process's display unit */
    asid = PROC1_$AS_ID;
    unit_num = SMD_GLOBALS.asid_to_unit[asid];

    /* 0x00e6eb60-0x00e6eb70: A0 = 0xE2E3FC + unit*0x10C (mulu.w here), and
     * the hw pointer is the record's first field at (-0xF4,A0). */
    hw = smd_$unit_rec((int16_t)unit_num)->hw;

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

        /*
         * 00e6eb80 move.l (0x4,A3),D0 / addq.l #1,D0 / move.l D0,(-0x8,A6)
         * 00e6eb8a moveq #0x28,D1 / add.l (A4),D1 / move.l D1,(-0x4,A6)
         * Both wait targets are latched here, before the state test.
         */
        lock_ec_target = (int32_t)hw->lock_ec.count + 1;
        clock_target = (int32_t)(0x28 + TIME_$CLOCKH);

        /* 00e6eb92 cmpi.w #1,(A2) / bne / st (0x20,A3) */
        if (*lock_data == 1) {
            hw->field_20 = 0xFF;
        }

        /*
         * 00e6eb9c move.w (0x2,A3),D0w / subq.w #1,D0w / cmpi.w #7,D0w / bcc
         * 00e6eba8 jump table at 0x00E6EBB2 (7 words, read with gsk):
         *   states 1,2,3,4,5,7 -> 0x00E6EBC0 (the EC_$WAIT below)
         *   state  6           -> 0x00E6EBEA (clr.w (0x2,A3))
         * Any other state (0, or > 7) skips straight to 0x00E6EBEE, which
         * only clears field_20 - it does NOT reset lock_state.
         */
        lock_state = hw->lock_state;

        switch (lock_state) {
            case 1:  /* Regular lock */
            case 2:  /* Scroll setup */
            case 3:  /* Scroll complete */
            case 4:  /* Scroll cleanup */
            case 5:  /* Initial lock */
            case 7:  /* EOF wait */
                /*
                 * 00e6ebc0-00e6ebde: EC_$WAIT with two 3-element arrays by
                 * value.  The values were computed at the top of the loop
                 * body (0x00E6EB80/0x00E6EB8A), before the state switch:
                 *   ec[0]  = &hw->lock_ec,   val[0] = lock_ec.value + 1
                 *   ec[1]  = TIME_$CLOCKH,   val[1] = TIME_$CLOCKH + 0x28
                 *   ec[2]  = NULL,           val[2] = 0
                 * so the wait also ends 0x28 clock ticks from now.
                 */
                wait_result = EC_$WAIT(
                    (ec_$wait_ecs_t){ { &hw->lock_ec,
                                        (ec_$eventcount_t *)&TIME_$CLOCKH,
                                        NULL } },
                    (ec_$wait_vals_t){ { lock_ec_target, clock_target, 0 } });

                /* 00e6ebe2 tst.w D0w / sne D3b / tst.b D3b / bpl */
                if (wait_result != 0) {
                    /* 00e6ebea clr.w (0x2,A3) */
                    hw->lock_state = SMD_LOCK_STATE_UNLOCKED;
                }
                break;

            case 6:  /* Clear state */
                /* 00e6ebea clr.w (0x2,A3) */
                hw->lock_state = SMD_LOCK_STATE_UNLOCKED;
                break;

            default:
                /* 0x00E6EBEE: nothing but the field_20 clear below. */
                break;
        }

        /* Clear the field_20 flag before next iteration */
        hw->field_20 = 0;
    }
}
