/*
 * smd/rel_display.c - SMD_$REL_DISPLAY implementation
 *
 * Release display lock.
 *
 * Original address: 0x00E6EC10
 *
 * This function releases the display lock acquired by SMD_$ACQ_DISPLAY.
 * If a scroll operation is in progress (lock_state == 4), it continues
 * the scroll. Otherwise, it clears the lock state and advances the
 * lock eventcount to wake any waiters.
 *
 * Assembly:
 *   00e6ec10    link.w A6,-0xc
 *   00e6ec14    movem.l {  A5 A3 A2},-(SP)
 *   00e6ec18    lea (0xe82b8c).l,A5           ; A5 = globals base
 *   00e6ec1e    move.w (0x00e2060a).l,D0w     ; D0 = PROC1_$AS_ID
 *   00e6ec24    add.w D0w,D0w                 ; D0 *= 2
 *   00e6ec26    move.w (0x48,A5,D0w*0x1),D0w  ; D0 = asid_to_unit[ASID]
 *   00e6ec2a    movea.l #0xe2e3fc,A0          ; A0 = display units base
 *   00e6ec30    move.w D0w,D1w
 *   00e6ec32    mulu.w #0x10c,D1              ; D1 = unit * 0x10c
 *   00e6ec36    lea (0x0,A0,D1*0x1),A2        ; A2 = unit structure
 *   00e6ec3a    movea.l (-0xf4,A2),A3         ; A3 = hw pointer
 *   00e6ec3e    cmpi.w #0x4,(0x2,A3)          ; if (hw->lock_state == 4)
 *   00e6ec44    bne.b 0x00e6ec56
 *   00e6ec46    move.l (0x8,A2),-(SP)         ; push ec from unit+8
 *   00e6ec4a    pea (A3)                      ; push hw
 *   00e6ec4c    jsr 0x00e272b2.l              ; SMD_$CONTINUE_SCROLL
 *   00e6ec52    addq.w #0x8,SP
 *   00e6ec54    bra.b 0x00e6ec5a
 *   00e6ec56    clr.w (0x2,A3)                ; hw->lock_state = 0
 *   00e6ec5a    pea (0x4,A3)                  ; push &hw->lock_ec
 *   00e6ec5e    jsr 0x00e206ee.l              ; EC_$ADVANCE
 *   00e6ec64    movem.l (-0x18,A6),{  A2 A3 A5}
 *   00e6ec6a    unlk A6
 *   00e6ec6c    rts
 */

#include "smd/smd_internal.h"

/*
 * SMD_$REL_DISPLAY - Release display lock
 *
 * Releases the display lock acquired by SMD_$ACQ_DISPLAY.
 * If a scroll operation is pending (lock_state == 4), continues the
 * scroll operation. Otherwise, clears the lock state.
 *
 * Always advances the lock eventcount to signal any waiting processes.
 */
void SMD_$REL_DISPLAY(void)
{
    smd_display_hw_t *hw;
    smd_display_unit_t *unit_ptr;
    ec_$eventcount_t *ec;
    uint16_t asid;
    uint16_t unit_num;

    /* Get current process's display unit */
    asid = PROC1_$AS_ID;
    unit_num = SMD_GLOBALS.asid_to_unit[asid];

    /* Get display unit structure */
    unit_ptr = smd_get_unit(unit_num);
    hw = unit_ptr->hw;

    /* Check if we're in scroll-cleanup state */
    if (hw->lock_state == SMD_LOCK_STATE_LOCKED_4) {
        /* Continue the scroll operation */
        /* The ec is at unit_ptr->field_08 in the original, which maps to
         * an eventcount used for scroll completion signaling */
        ec = (ec_$eventcount_t *)unit_ptr->field_08;
        SMD_$CONTINUE_SCROLL(hw, ec);
    } else {
        /* Clear the lock state */
        hw->lock_state = SMD_LOCK_STATE_UNLOCKED;
    }

    /* Advance the lock eventcount to wake any waiters */
    EC_$ADVANCE(&hw->lock_ec);
}
