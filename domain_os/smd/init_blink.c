/*
 * smd/init_blink.c - SMD_$INIT_BLINK implementation
 *
 * Initialize cursor blinking state.
 *
 * Original address: 0x00E34EB2
 *
 * Assembly:
 *   link.w A6,-0x4
 *   movem.l {A5 D2},-(SP)
 *   lea (0xe35380).l,A5        ; unused (A5 base)
 *   movea.l #0xe273d6,A0       ; SMD_BLINK_STATE address
 *   clr.b (A0)                 ; smd_time_com = 0
 *   st (0x2,A0)                ; blink_flag = 0xFF (enabled)
 *   clr.w (0x4,A0)             ; blink_counter = 0
 *   movea.l #0xe82b8c,A1       ; SMD_GLOBALS base
 *   pea (0x1d98,A1)            ; push &SMD_DEFAULT_DISPLAY_UNIT (A5+0x1D98)
 *   jsr SMD_$INQ_DISP_TYPE     ; call to check if display exists
 *   addq.w #0x4,SP
 *   tst.w D0w                  ; test result
 *   sne D2b                    ; D2 = (result != 0) ? 0xFF : 0
 *   tst.b D2b
 *   bpl.b done                 ; if D2 >= 0 (no display), skip
 *   move.l #0x1e848,-(SP)      ; push 125000 (blink interval in microseconds)
 *   jsr smd_$reschedule_blink_timer
 * done:
 *   movem.l (-0xc,A6),{D2 A5}
 *   unlk A6
 *   rts
 *
 * The blink interval is 125000 microseconds = 125ms = 8 blinks per second.
 */

#include "smd/smd_internal.h"

/* Blink interval in microseconds (125ms) */
#define SMD_BLINK_INTERVAL_US   125000

/*
 * SMD_$INIT_BLINK - Initialize cursor blinking
 *
 * Sets up the initial cursor blink state and schedules the blink timer
 * if a display is present.
 *
 * Called during system initialization after the display subsystem is ready.
 */
void SMD_$INIT_BLINK(void)
{
    uint16_t disp_type;
    int8_t has_display;

    /* Initialize blink state */
    SMD_BLINK_STATE.smd_time_com = 0;      /* Time communication flag = 0 */
    SMD_BLINK_STATE.blink_flag = 0xFF;     /* Blink enabled */
    SMD_BLINK_STATE.blink_counter = 0;     /* Counter = 0 */

    /* Check if default display exists */
    disp_type = SMD_$INQ_DISP_TYPE(&SMD_DEFAULT_DISPLAY_UNIT);

    has_display = (disp_type != 0) ? -1 : 0;

    if (has_display < 0) {
        /* Display exists - schedule blink timer */
        smd_$reschedule_blink_timer(SMD_BLINK_INTERVAL_US);
    }
}
