/*
 * smd/horiz_line.c - SMD_$HORIZ_LINE implementation
 *
 * Draws a horizontal line using hardware BLT acceleration.
 * This is an internal function called by SMD_$DRAW_BOX.
 *
 * Original address: 0x00E8496A (thunk) -> 0x00E70760 (actual code)
 */

#include "smd/smd_internal.h"

/*
 * SMD_$HORIZ_LINE - Draw horizontal line
 *
 * Programs the hardware BLT registers to draw a single horizontal line
 * from (x1, y) to (x2, y). The line is drawn using the hardware's
 * line drawing mode with pattern 0x3C0.
 *
 * Parameters:
 *   y       - Pointer to Y coordinate
 *   x1      - Pointer to starting X coordinate
 *   x2      - Pointer to ending X coordinate
 *   param4  - Unused (passed through from caller)
 *   hw_regs - Pointer to hardware BLT registers
 *   control - Pointer to control value from SMD_$ACQ_DISPLAY
 *   param7  - Unused (passed through from caller)
 *
 * The function programs the hardware registers and then busy-waits
 * for the operation to complete by polling the control register
 * until the busy bit (bit 15) clears.
 *
 * Original address: 0x00E8496A
 *
 * Assembly (main routine at 0x00E70760):
 *   00e70760    movem.l {  A4 A3 A2 D6 D5 D4 D3 D2},-(SP)
 *   00e70764    movem.l (0x24,SP),{  A2 A3 A4}  ; A2=y, A3=x1, A4=x2
 *   00e7076a    movea.l (0x34,SP),A1            ; A1=hw_regs
 *   00e7076e    move.w (A2),(0xc,A1)            ; y_start = *y
 *   00e70772    move.w #-0x1,(0x8,A1)           ; y_extent = 0xFFFF (single row)
 *   00e70778    move.w (A3),D0w                 ; D0 = *x1
 *   00e7077a    move.w D0w,(0xe,A1)             ; x_start = *x1
 *   00e7077e    move.w (A4),D1w                 ; D1 = *x2
 *   00e70780    and.w #0xf,D1w                  ; D1 = x2 & 0xF
 *   00e70784    move.w D1w,(0x2,A1)             ; bit_pos = x2 & 0xF
 *   00e70788    move.w (A4),D1w                 ; D1 = *x2
 *   00e7078a    lsr.w #0x4,D1w                  ; D1 = x2 >> 4 (word offset)
 *   00e7078c    lsr.w #0x4,D0w                  ; D0 = x1 >> 4 (word offset)
 *   00e7078e    sub.w D0w,D1w                   ; D1 = (x2 >> 4) - (x1 >> 4)
 *   00e70790    blt.b 0x00e70794                ; if negative, skip negate
 *   00e70792    neg.w D1w                       ; D1 = -D1 (take absolute)
 *   00e70794    subq.w #0x1,D1w                 ; D1 = width - 1
 *   00e70796    move.w D1w,(0xa,A1)             ; x_extent = width - 1
 *   00e7079a    move.w #0x3c0,(0x6,A1)          ; pattern = 0x3C0 (draw)
 *   00e707a0    move.w #0x3ff,(0x4,A1)          ; mask = 0x3FF
 *   00e707a6    movea.l (0x30,SP),A2            ; A2 = param4 (unused)
 *   00e707aa    movea.l (0x38,SP),A0            ; A0 = control
 *   00e707ae    move.w (A0),D0w                 ; D0 = *control
 *   00e707b0    or.w #-0x7ff2,D0w               ; D0 |= 0x800E (start + draw)
 *   00e707b4    movea.l (0x3c,SP),A0            ; A0 = param7 (unused)
 *   00e707b8    move.w D0w,(A1)                 ; control = D0 (start operation)
 *   00e707ba    tst.w (A1)                      ; test control register
 *   00e707bc    bmi.b 0x00e707ba                ; loop while bit 15 set (busy)
 *   00e707be    movem.l (SP)+,{  D2 D3 D4 D5 D6 A2 A3 A4}
 *   00e707c2    rts
 */
void SMD_$HORIZ_LINE(int16_t *y, int16_t *x1, int16_t *x2, void *param4,
                     smd_hw_blt_regs_t *hw_regs, uint16_t *control, void *param7)
{
    uint16_t x1_val, x2_val;
    int16_t width;

    (void)param4;   /* Unused */
    (void)param7;   /* Unused */

    x1_val = (uint16_t)*x1;
    x2_val = (uint16_t)*x2;

    /* Set Y coordinate - single row */
    hw_regs->y_start = (uint16_t)*y;
    hw_regs->y_extent = SMD_BLT_SINGLE_LINE;

    /* Set X start coordinate */
    hw_regs->x_start = x1_val;

    /* Set bit position within starting word */
    hw_regs->bit_pos = x2_val & 0x0F;

    /*
     * Calculate width in words.
     * Width = |(x2 >> 4) - (x1 >> 4)| - 1
     *
     * The original code computes: (x2>>4) - (x1>>4), then negates
     * if the result is non-negative (which gives |x1-x2|>>4 effectively).
     */
    width = (int16_t)((x2_val >> 4) - (x1_val >> 4));
    if (width >= 0) {
        width = -width;
    }
    width = width - 1;
    hw_regs->x_extent = (uint16_t)width;

    /* Set pattern for line drawing */
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
