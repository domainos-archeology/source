/*
 * smd/start_scroll.c - SMD_$START_SCROLL implementation
 *
 * Initiates a hardware scroll operation on the display.
 *
 * Original address: 0x00E272A8 (trampoline), 0x00E15C68 (implementation)
 *
 * Resolved (bead source-a2ip): the original is hand-written assembly - no
 * link/unlk, a bare "move.l A5,-(SP)" thunk, arguments read off SP at
 * (0x8,SP)/(0xc,SP), a dispatch base handed in through A0 by the trampoline,
 * and a call made by jumping INTO the dispatch block with "jsr (0x150,A5)" -
 * so it now lives in smd/sau2/scroll.s.  What follows is a portable model of
 * the same behaviour, compiled only when ARCH_M68K is not defined so the two
 * do not collide at link time (the same convention as smd/start_blt.c).
 */

#include "smd/smd_internal.h"

/*
 * smd_$setup_scroll_blt - SAU-specific scroll BLT setup
 *
 * This is an internal function that sets up the BLT registers for
 * the scroll operation. It is called via the SAU dispatch table
 * at offset 0x150 from the table base.
 *
 * The function:
 * 1. Decrements scroll_dy by 2 (or sets it to 0 if less)
 * 2. Sets field_24 flag based on remaining scroll amount
 * 3. Advances the operation event count if scroll is complete
 * 4. Based on scroll direction (scroll_dx), sets up BLT parameters:
 *    - Direction 0: Scroll down (content moves up)
 *    - Direction 1: Scroll up (content moves down)
 *    - Direction 2: Scroll right (content moves left)
 *    - Direction 3: Scroll left (content moves right)
 *
 * Parameters:
 *   blt_regs - Pointer to BLT register block (passed via A0)
 *   hw       - Display hardware info (passed via A1)
 *
 * Returns:
 *   BLT control flags (0x04 or 0x0C depending on direction)
 *
 * Original address: 0x00E27070
 */
/* smd_$setup_scroll_blt is declared in smd/smd_internal.h */

/*
 * SMD_$START_SCROLL - Start scroll operation
 *
 * Initiates a hardware scroll operation. This function:
 * 1. Sets the display lock state to SCROLL (2)
 * 2. Sets video flag bit 5 (0x20) to indicate scroll in progress
 * 3. Clears field_20
 * 4. Saves the operation event count to field_1c
 * 5. Calls the SAU-specific BLT setup function
 * 6. Writes the BLT control value to the event count location
 *
 * Parameters:
 *   hw        - Display hardware info structure
 *   ctrl_regs - Display controller register base (the unit record's +0xFC
 *               field; SMD_$SOFT_SCROLL pushes it by value at 0x00E6F38C)
 *
 * Original assembly at 0x00E15C68:
 *   move.l A5,-(SP)              ; Save A5
 *   movea.l A0,A5                ; A5 = dispatch table base
 *   movea.l (0x8,SP),A1          ; A1 = hw parameter
 *   movea.l (0xc,SP),A0          ; A0 = ctrl_regs parameter
 *   move.w #0x2,(0x2,A1)         ; hw->lock_state = 2 (SCROLL)
 *   ori.w #0x20,(0x22,A1)        ; hw->video_flags |= 0x20
 *   clr.w (0x20,A1)              ; hw->field_20 = 0
 *   move.l (0x10,A1),(0x1c,A1)   ; hw->field_1c = hw->op_ec
 *   jsr (0x150,A5)               ; Call smd_$setup_scroll_blt
 *   or.w (0x22,A1),D0w           ; D0 |= hw->video_flags
 *   or.w #-0x7ff0,D0w            ; D0 |= 0x8010
 *   move.w D0w,(A0)              ; ctrl_regs[0] = D0 (starts BLT)
 *   movea.l (SP)+,A5             ; Restore A5
 *   rts
 */
#if !defined(ARCH_M68K)

void SMD_$START_SCROLL(smd_display_hw_t *hw, SMD_HW_REG_PTR ctrl_regs)
{
    uint16_t blt_ctl;

    /* Set lock state to indicate scroll in progress */
    hw->lock_state = SMD_LOCK_STATE_SCROLL;

    /* Set video flag bit 5 to indicate scroll operation */
    hw->video_flags |= 0x20;

    /* "clr.w (0x20,A1)" clears the whole word at +0x20, i.e. both bytes */
    hw->field_20 = 0;
    hw->field_21 = 0;

    /* Save operation event count to field_1c */
    /* This preserves the EC state during the scroll */
    hw->field_1c = *(uint32_t *)&hw->op_ec;

    /* Call SAU-specific BLT setup function
     * This sets up the BLT registers and returns control flags */
    blt_ctl = smd_$setup_scroll_blt(ctrl_regs, hw);

    /* Combine BLT control with video flags and start bit
     * 0x8010 = start bit (0x8000) + unknown flags (0x10) */
    blt_ctl |= hw->video_flags;
    blt_ctl |= 0x8010;

    /* Write to BLT control register to start the operation */
    ctrl_regs[0] = blt_ctl;
}

#endif /* !ARCH_M68K */
