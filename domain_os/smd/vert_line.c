/*
 * smd/vert_line.c - SMD_$VERT_LINE implementation
 *
 * Draws a vertical line using hardware BLT acceleration.
 * This is an internal function called by SMD_$DRAW_BOX.
 *
 * Original address: 0x00E84974 (thunk) -> 0x00E707C4 (actual code)
 */

#include "smd/smd_internal.h"

/*
 * SMD_$VERT_LINE - Draw vertical line
 *
 * Programs the hardware BLT registers to draw a single vertical line
 * from (x, y1) to (x, y2). The line is drawn using the hardware's
 * line drawing mode with pattern 0x3C0.
 *
 * Parameters:
 *   x       - Pointer to X coordinate
 *   y1      - Pointer to starting Y coordinate
 *   y2      - Pointer to ending Y coordinate
 *   param4  - Unused (passed through from caller)
 *   hw_regs - Pointer to hardware BLT registers
 *   control - Pointer to control value from SMD_$ACQ_DISPLAY
 *
 * The function programs the hardware registers and then busy-waits
 * for the operation to complete by polling the control register
 * until the busy bit (bit 15) clears.
 *
 * Note: This function shares the final register setup and wait loop
 * with SMD_$HORIZ_LINE (jumps to 0x00E7079A).
 *
 * Original address: 0x00E84974
 *
 * Assembly (main routine at 0x00E707C4):
 *   00e707c4    movem.l {  A4 A3 A2 D6 D5 D4 D3 D2},-(SP)
 *   00e707c8    movem.l (0x24,SP),{  A2 A3 A4}  ; A2=x, A3=y1, A4=y2
 *   00e707ce    movea.l (0x34,SP),A1            ; A1=hw_regs
 *   00e707d2    move.w (A2),D0w                 ; D0 = *x
 *   00e707d4    move.w D0w,(0xe,A1)             ; x_start = *x
 *   00e707d8    and.w #0xf,D0w                  ; D0 = x & 0xF
 *   00e707dc    move.w D0w,(0x2,A1)             ; bit_pos = x & 0xF
 *   00e707e0    move.w #-0x1,(0xa,A1)           ; x_extent = 0xFFFF (single col)
 *   00e707e6    move.w (A3),D0w                 ; D0 = *y1
 *   00e707e8    move.w D0w,(0xc,A1)             ; y_start = *y1
 *   00e707ec    sub.w (A4),D0w                  ; D0 = y1 - *y2
 *   00e707ee    blt.b 0x00e707f2                ; if negative, skip negate
 *   00e707f0    neg.w D0w                       ; D0 = -D0 (take absolute)
 *   00e707f2    subq.w #0x1,D0w                 ; D0 = height - 1
 *   00e707f4    move.w D0w,(0x8,A1)             ; y_extent = height - 1
 *   00e707f8    bra.b 0x00e7079a                ; jump to shared code
 *
 * Shared code at 0x00E7079A (from HORIZ_LINE):
 *   00e7079a    move.w #0x3c0,(0x6,A1)          ; pattern = 0x3C0
 *   00e707a0    move.w #0x3ff,(0x4,A1)          ; mask = 0x3FF
 *   00e707a6    movea.l (0x30,SP),A2            ; (unused param)
 *   00e707aa    movea.l (0x38,SP),A0            ; A0 = control
 *   00e707ae    move.w (A0),D0w                 ; D0 = *control
 *   00e707b0    or.w #-0x7ff2,D0w               ; D0 |= 0x800E
 *   00e707b4    movea.l (0x3c,SP),A0            ; (unused param)
 *   00e707b8    move.w D0w,(A1)                 ; start operation
 *   00e707ba    tst.w (A1)                      ; test control
 *   00e707bc    bmi.b 0x00e707ba                ; loop while busy
 */
void SMD_$VERT_LINE(int16_t *x, int16_t *y1, int16_t *y2, void *param4,
                    smd_hw_blt_regs_t *hw_regs, uint16_t *control)
{
    uint16_t x_val, y1_val, y2_val;
    int16_t height;

    (void)param4;   /* Unused */

    x_val = (uint16_t)*x;
    y1_val = (uint16_t)*y1;
    y2_val = (uint16_t)*y2;

    /* Set X coordinate - single column */
    hw_regs->x_start = x_val;
    hw_regs->bit_pos = x_val & 0x0F;
    hw_regs->x_extent = SMD_BLT_SINGLE_LINE;

    /* Set Y start coordinate */
    hw_regs->y_start = y1_val;

    /*
     * Calculate height.
     * Height = |y1 - y2| - 1
     */
    height = (int16_t)(y1_val - y2_val);
    if (height >= 0) {
        height = -height;
    }
    height = height - 1;
    hw_regs->y_extent = (uint16_t)height;

    /* Set pattern for line drawing (shared with HORIZ_LINE) */
    hw_regs->pattern = SMD_BLT_PATTERN_DRAW;

    /* Set default mask */
    hw_regs->mask = SMD_BLT_DEFAULT_MASK;

    /* Start the BLT operation */
    hw_regs->control = *control | SMD_BLT_CMD_START_DRAW;

    /* Busy-wait for completion */
    while ((int16_t)hw_regs->control < 0) {
        /* Spin until bit 15 clears */
    }
}
