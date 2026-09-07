/*
 * smd/blink_cursor_1.c - SMD_$BLINK_CURSOR_1 implementation
 *
 * Handles cursor blinking for the primary display (unit 1).
 * Called from the cursor blink timer interrupt.
 *
 * Original address: 0x00E2722C
 */

#include "smd/smd_internal.h"

/*
 * The two PC-relative `lea`s resolve to the two SMD data objects, not to
 * anonymous addresses (pea (d,PC) / lea (d,PC) target = instruction + 2 + d):
 *
 *   00e27248  lea (0x18e,PC),A1 -> 0x00e2724a + 0x18e = 0x00E273D8
 *                                = &SMD_TIME_$COM.cursor_painted
 *   00e2724c  lea (0x128,PC),A0 -> 0x00e2724e + 0x128 = 0x00E27376
 *                                = SMD_$DISPLAY_COM = &SMD_DISPLAY_INFO[0]
 *
 * so every (d,A0) below is a field of unit 1's hardware record and A1/A2 is
 * SMD_TIME_$COM's "cursor is painted" flag - the very flag
 * SMD_$BLINK_CURSOR_CALLBACK re-tests at 0x00E6FF8E right after calling this
 * routine through SMD_BLINK_FUNC_PTABLE.  (An earlier version of this file
 * had 0x00E27316 for A0, which is 0x60 low.)
 */

/* The last two arguments of SMD_$XOR_CURSOR are passed by value:
 * the display memory base and the controller register base of unit 1
 * (0x00E27256 and 0x00E27250 push the same longwords SMD_$INIT plants in
 * the unit record at +0x108 and +0xFC). */
#define SMD_UNIT1_DISPLAY_BASE   0x00FC0000u
#define SMD_UNIT1_CTRL_REGS      ((SMD_HW_REG_PTR)0x00FF9800u)

/* The display-valid probe at 0x00E2723E reads the controller register
 * directly rather than through the unit record. */
#define SMD_UNIT1_STATUS_REG     (*(volatile int16_t *)0x00FF9800)

/*
 * SMD_$BLINK_CURSOR_1 - Blink cursor for unit 1
 *
 * This function handles cursor blinking for the primary display.
 * It is called from a timer interrupt and toggles the cursor
 * visibility by calling the low-level cursor draw routine.
 *
 * The function only operates if the display hardware is valid
 * (checked by reading from 0x00FF9800).
 *
 * Original address: 0x00E2722C
 *
 * Assembly:
 *   00e2722c    movem.l {  A5 A3 A2 D2},-(SP)
 *   00e27230    lea (-0x312,PC),A5            ; local globals ptr
 *   00e27234    move SR,-(SP)                 ; save status register
 *   00e27236    ori #0x700,SR                 ; disable interrupts (IPL=7)
 *   00e2723a    lea (-0x31c,PC),A5            ; adjust A5
 *   00e2723e    tst.w (0x00ff9800).l          ; check display valid
 *   00e27244    bmi.w 0x00e27278              ; if negative, skip
 *   00e27248    lea (0x18e,PC),A1             ; cursor state
 *   00e2724c    lea (0x128,PC),A0             ; display communication
 *   00e27250    move.l #0xff9800,-(SP)        ; ec_2
 *   00e27256    move.l #0xfc0000,-(SP)        ; ec_1
 *   00e2725c    movea.l A1,A2                 ; save cursor state ptr
 *   00e2725e    pea (A1)                      ; cursor flag ptr
 *   00e27260    pea (A0)                      ; display comm
 *   00e27262    pea (0x4e,A0)                 ; hw offset
 *   00e27266    pea (0x32,A0)                 ; cursor pos offset
 *   00e2726a    pea (0x36,A0)                 ; cursor num offset
 *   00e2726e    jsr 0x00e2720e                ; draw_cursor internal
 *   00e27272    adda.w #0x1c,SP
 *   00e27276    not.b (A2)                    ; toggle blink flag
 *   00e27278    move (SP)+,SR                 ; restore status
 *   00e2727a    movem.l (SP)+,{  D2 A2 A3 A5}
 *   00e2727e    rts
 */
void SMD_$BLINK_CURSOR_1(void)
{
    /* Save status register and disable interrupts */
    /* Note: In C we can't directly manipulate SR, this would need
     * assembly or a platform-specific intrinsic.
     * For now, we represent the logic flow. */

    /* 00e2723e tst.w (0x00ff9800).l / 00e27244 bmi -> skip */
    if (SMD_UNIT1_STATUS_REG >= 0) {
        /* Display is valid, perform cursor blink */

        smd_display_hw_t *hw = smd_$unit_info(1);   /* A0 = 0x00E27376 */

        /*
         * Arguments are pushed right to left, so the last `pea` is the first
         * argument:
         *   00e2726a pea (0x36,A0)  -> &hw->cursor_number  (0x00E273AC)
         *   00e27266 pea (0x32,A0)  -> &hw->cursor_pos     (0x00E273A8)
         *   00e27262 pea (0x4e,A0)  -> &hw->min_x          (0x00E273C4)
         *   00e27260 pea (A0)       -> hw                  (0x00E27376)
         *   00e2725e pea (A1)       -> &SMD_TIME_$COM.cursor_painted
         *   00e27256 move.l #0xfc0000,-(SP)  display_base, by value
         *   00e27250 move.l #0xff9800,-(SP)  ctrl_regs,   by value
         */
        SMD_$XOR_CURSOR(&hw->cursor_number,
                        &hw->cursor_pos,
                        &hw->min_x,
                        hw,
                        &SMD_TIME_$COM.cursor_painted,
                        SMD_UNIT1_DISPLAY_BASE,
                        SMD_UNIT1_CTRL_REGS);

        /* 00e27276 not.b (A2): one's complement of the whole byte, which
         * flips the Domain boolean between 0x00 and 0xFF. */
        SMD_TIME_$COM.cursor_painted = (boolean)~SMD_TIME_$COM.cursor_painted;
    }

    /* Status register restored implicitly when function returns */
}
