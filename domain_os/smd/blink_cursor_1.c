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
 * Display state block at 0x00E27376 (for unit 1, offset 0x60 bytes)
 * Used for cursor display parameters.
 *
 * Cursor state structure at 0x00E273D8:
 *   - blink_flag (byte at 0x00E273D8): current blink state
 */

/* Fixed display state addresses for unit 1 */
#define SMD_UNIT1_DISPLAY_COM    ((void *)0x00E27316)  /* Display communication area */
#define SMD_UNIT1_CURSOR_STATE   ((int8_t *)0x00E273D8)
#define SMD_UNIT1_CURSOR_NUM     ((int16_t *)0x00E273AC)
#define SMD_UNIT1_CURSOR_POS     ((uint32_t *)0x00E273A8)
#define SMD_UNIT1_DISPLAY_HW     ((void *)0x00E273C4)
#define SMD_UNIT1_EC_1           ((uint32_t *)0x00FC0000)
#define SMD_UNIT1_EC_2           ((uint32_t *)0x00FF9800)

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
    uint16_t saved_sr;

    /* Save status register and disable interrupts */
    /* Note: In C we can't directly manipulate SR, this would need
     * assembly or a platform-specific intrinsic.
     * For now, we represent the logic flow. */

    /* Check if display hardware is valid */
    if (*(volatile int16_t *)0x00FF9800 >= 0) {
        /* Display is valid, perform cursor blink */

        /* Call internal cursor draw routine
         * Parameters from original assembly:
         *   - cursor_num ptr: 0x00E273AC (offset 0x36 from 0x00E27376)
         *   - cursor_pos ptr: 0x00E273A8 (offset 0x32 from 0x00E27376)
         *   - display_hw+0x4E: 0x00E273C4
         *   - display_comm: 0x00E27376
         *   - cursor_flag: 0x00E273D8
         *   - ec_1: 0x00FC0000
         *   - ec_2: 0x00FF9800
         */
        smd_$draw_cursor_internal(SMD_UNIT1_CURSOR_NUM,
                                   SMD_UNIT1_CURSOR_POS,
                                   (void *)((uintptr_t)SMD_UNIT1_DISPLAY_COM + 0x4E),
                                   SMD_UNIT1_DISPLAY_COM,
                                   SMD_UNIT1_CURSOR_STATE,
                                   SMD_UNIT1_EC_1,
                                   SMD_UNIT1_EC_2);

        /* Toggle blink flag */
        *SMD_UNIT1_CURSOR_STATE = ~(*SMD_UNIT1_CURSOR_STATE);
    }

    /* Status register restored implicitly when function returns */
}
