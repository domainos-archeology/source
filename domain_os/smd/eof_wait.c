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
    int16_t unit_idx;             /* D2 */
    uint16_t saved_state;         /* D3 */
    smd_display_unit_t *rec;      /* A3 - 0xF4 */
    smd_display_hw_t *hw;         /* A4 */
    uint32_t target_count;        /* (-0x4,A6) / D0 */
    int16_t wait_result;          /* D0 */
    uint16_t asid;

    asid = PROC1_$AS_ID;

    /* 00e6f3bc move.w PROC1_$AS_ID,D0w / add.w D0w,D0w
     * 00e6f3c8 move.w (0x48,A5,D0w*1),D2w / bne */
    unit_idx = (int16_t)SMD_GLOBALS.asid_to_unit[asid];
    if (unit_idx == 0) {
        *status_ret = status_$display_invalid_use_of_driver_procedure;
        return;
    }

    /* 00e6f3d8 pea (-0x1aae,PC) -> 0x00E6D92C / bsr SMD_$ACQ_DISPLAY
     * 00e6f3e2 move.w D0w,D3w */
    saved_state = SMD_$ACQ_DISPLAY(&SMD_ACQ_LOCK_DATA);

    /* 00e6f3e4-00e6f3f4: mulu.w #0x10c (unsigned here; the unit number is
     * never negative on this path) then the usual -0xF4 bias. */
    rec = smd_$unit_rec(unit_idx);
    hw = rec->hw;

    /* 00e6f3f8 move.l (0x4,A4),D0 / addq.l #1,D0 */
    target_count = (uint32_t)hw->lock_ec.count + 1;

    /* 00e6f402 move.w #7,(0x2,A4) */
    hw->lock_state = 7;

    /* 00e6f408 movea.l (0x8,A3),A1 / move.w #0x21,(A1):
     * arm the controller's end-of-frame interrupt. */
    *rec->ctrl_regs = 0x21;

    /*
     * 00e6f410-00e6f44c: EC_$WAIT with two 3-element arrays pushed by value.
     *   ec[0] = &hw->lock_ec        (pea (0x4,A4))
     *   ec[1] = &FIM_$WIRED_DATA.quit_ec[asid] (0x00E22002 + asid*12)
     *   ec[2] = NULL                (move.l #0,-(SP))
     *   val[0] = target_count
     *   val[1] = FIM_$WIRED_DATA.quit_value[asid] + 1   (0x00E222BA + asid*4)
     *   val[2] = 0
     */
    wait_result = EC_$WAIT(
        (ec_$wait_ecs_t){ { &hw->lock_ec, &FIM_$WIRED_DATA.quit_ec[asid], NULL } },
        (ec_$wait_vals_t){ { (int32_t)target_count,
                             (int32_t)(FIM_$WIRED_DATA.quit_value[asid] + 1),
                             0 } });

    /* 00e6f456 tst.w D0w / seq D2b / tst.b D2b / bpl */
    if (wait_result == 0) {
        /* 00e6f45e clr.l (A2) */
        *status_ret = status_$ok;
    } else {
        /* 00e6f462 move.l #0x130022,(A2)
         * 00e6f486 move.l (0,A0,D2w),(0,A1,D0w) with A0 = FIM_$QUIT_EC base
         *          scaled by 12 and A1 = FIM_$QUIT_VALUE base scaled by 4:
         *          remember the quit eventcount value that woke us. */
        *status_ret = status_$display_quit_while_waiting;
        FIM_$WIRED_DATA.quit_value[asid] = (uint32_t)FIM_$WIRED_DATA.quit_ec[asid].count;
    }

    /* 00e6f48c movea.l (0x8,A3),A1 / move.w D3w,(A1) */
    *rec->ctrl_regs = saved_state;

    /* 00e6f492 bsr SMD_$REL_DISPLAY */
    SMD_$REL_DISPLAY();
}
