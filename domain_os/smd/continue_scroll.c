/*
 * smd/continue_scroll.c - SMD_$CONTINUE_SCROLL implementation
 *
 * Continues a hardware scroll operation on the display.
 *
 * Original address: 0x00E272B2 (trampoline), 0x00E15C9C (implementation)
 */

#include "smd/smd_internal.h"

/*
 * smd_$setup_scroll_blt - SAU-specific scroll BLT setup
 * (Declared in start_scroll.c, defined in SAU-specific code)
 */
extern uint16_t smd_$setup_scroll_blt(uint16_t *blt_regs, smd_display_hw_t *hw);

/*
 * SMD_$CONTINUE_SCROLL - Continue scroll operation
 *
 * Continues a hardware scroll operation that was previously started.
 * This function is typically called from an interrupt handler or
 * after the previous scroll step completes.
 *
 * If the remaining scroll amount (field_24) is zero, the scroll is
 * complete and the lock state is set to SCROLL_DONE (3).
 * Otherwise, another scroll step is initiated.
 *
 * Parameters:
 *   hw - Display hardware info structure
 *   ec - Event count for completion signaling (actually a BLT control pointer)
 *
 * Original assembly at 0x00E15C9C:
 *   move.l A5,-(SP)              ; Save A5
 *   movea.l A0,A5                ; A5 = dispatch table base
 *   movea.l (0x8,SP),A1          ; A1 = hw parameter
 *   movea.l (0xc,SP),A0          ; A0 = ec parameter (BLT control ptr)
 *   tst.w (0x24,A1)              ; Test hw->field_24
 *   bne.b continue               ; If non-zero, continue scrolling
 *   move.w #0x3,(0x2,A1)         ; hw->lock_state = 3 (SCROLL_DONE)
 *   movea.l (SP)+,A5             ; Restore A5
 *   rts
 * continue:
 *   jsr (0x150,A5)               ; Call smd_$setup_scroll_blt
 *   or.w (0x22,A1),D0w           ; D0 |= hw->video_flags
 *   or.w #-0x7ff0,D0w            ; D0 |= 0x8010
 *   move.w D0w,(A0)              ; *ec = D0 (starts BLT)
 *   move.w #0x2,(0x2,A1)         ; hw->lock_state = 2 (SCROLL)
 *   bra.b done
 * done:
 *   movea.l (SP)+,A5
 *   rts
 */
void SMD_$CONTINUE_SCROLL(smd_display_hw_t *hw, ec_$eventcount_t *ec)
{
    uint16_t blt_ctl;
    uint16_t *blt_ptr = (uint16_t *)ec;  /* ec is actually a BLT control ptr */

    /* Check if scroll is complete */
    if (hw->field_24 == 0) {
        /* Scroll complete - set lock state to SCROLL_DONE */
        hw->lock_state = SMD_LOCK_STATE_SCROLL_DONE;
        return;
    }

    /* More scrolling to do - set up the next BLT operation */
    blt_ctl = smd_$setup_scroll_blt(blt_ptr, hw);

    /* Combine BLT control with video flags and start bit */
    blt_ctl |= hw->video_flags;
    blt_ctl |= 0x8010;

    /* Write to BLT control register to start the operation */
    *blt_ptr = blt_ctl;

    /* Set lock state back to SCROLL to indicate operation in progress */
    hw->lock_state = SMD_LOCK_STATE_SCROLL;
}
