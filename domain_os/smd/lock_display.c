/*
 * smd/lock_display.c - portable model of SMD_$LOCK_DISPLAY
 *
 * Original address: 0x00E15CCE.
 *
 * The real routine is hand-written assembly and lives in
 * smd/sau2/lock_display.s; this file supplies a C stand-in for hosts that
 * are not the SAU2, and is compiled out entirely on m68k so the two do not
 * collide at link time (bead source-c3ap).
 *
 * The model is faithful about the state machine but not about the interrupt
 * discipline: the original brackets its body with "ori #0x700,SR" and
 * "andi #0xf8ff,SR", which *forces* IPL 0 on the way out instead of
 * restoring the caller's level.  ENABLE_INTERRUPTS() restores, so a caller
 * that ran at a raised IPL would come back at that IPL here and at 0 on the
 * hardware.
 */

#include "smd/smd_internal.h"

#if !defined(ARCH_M68K)

/*
 * SMD_$LOCK_DISPLAY - Lock display for exclusive access
 *
 * Parameters:
 *   hw        - the unit's hardware record; the lock state is hw->lock_state
 *               (offset +0x02)
 *   lock_data - caller's lock word.  Only the scroll-done transition looks at
 *               it, and that path also clears the word at +0x24.
 *
 * Returns:
 *   0xFF (negative) when the lock was taken, 0 when it was not.  The original
 *   only ever writes the low byte of D0 (clr.b / st), which is why the result
 *   is a byte.
 *
 * Lock state machine (0x00E15CD6-0x00E15D0E):
 *   0 (unlocked)    -> 5, success
 *   3 (scroll done) -> 4 plus lock_data[0x12] cleared, but only when
 *                      lock_data[0] == 1; success
 *   anything else   -> unchanged, failure
 */
int8_t SMD_$LOCK_DISPLAY(smd_display_hw_t *hw, int16_t *lock_data)
{
    int16_t state;
    uint16_t sr;

    DISABLE_INTERRUPTS(sr); /* 0x00E15CD2 ori #0x700,SR */

    state = (int16_t)hw->lock_state; /* 0x00E15CD6 */

    if (state == SMD_LOCK_STATE_UNLOCKED) {
        /* 0x00E15CE0 */
        hw->lock_state = SMD_LOCK_STATE_LOCKED_5;
        ENABLE_INTERRUPTS(sr); /* 0x00E15D0A andi #0xf8ff,SR */
        return (int8_t)0xFF;   /* 0x00E15D0E st D0b */
    }

    if (state == SMD_LOCK_STATE_SCROLL_DONE && lock_data[0] == 1) {
        /* 0x00E15D00 / 0x00E15D06 */
        hw->lock_state = SMD_LOCK_STATE_LOCKED_4;
        lock_data[0x12] = 0; /* clr.w (0x24,A1) */
        ENABLE_INTERRUPTS(sr);
        return (int8_t)0xFF;
    }

    /* 0x00E15CF8 / 0x00E15CFC */
    ENABLE_INTERRUPTS(sr);
    return 0;
}

#endif /* !ARCH_M68K */
