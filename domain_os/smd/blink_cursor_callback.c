/*
 * smd/blink_cursor_callback.c - SMD_$BLINK_CURSOR_CALLBACK implementation
 *
 * Timer callback function for handling cursor blinking and
 * display blanking timeout.
 *
 * Original address: 0x00E6FF56
 */

#include "smd/smd_internal.h"
#include "time/time.h"

/* Timer values for blink intervals */
#define BLINK_INTERVAL_NORMAL   0x1E848     /* 125000 - normal blink rate */
#define BLINK_INTERVAL_SLOW     0x3D090     /* 250000 - slow blink rate */

/*
 * SMD_$BLINK_CURSOR_CALLBACK - Cursor blink timer callback
 *
 * This function is called periodically by the timer system to:
 * 1. Handle cursor blinking by calling the unit-specific blink function
 * 2. Check and handle display blank timeout
 * 3. Manage trackpad cursor timeout
 *
 * The blink interval varies: normal when cursor is off, slower when on.
 *
 * Original address: 0x00E6FF56
 *
 * Assembly:
 *   00e6ff56    link.w A6,-0x10
 *   00e6ff5a    movem.l {  A5 A2},-(SP)
 *   00e6ff5e    lea (0xe82b8c).l,A5
 *   00e6ff64    move.l #0x1e848,(-0x8,A6)     ; default interval
 *   00e6ff6c    movea.l #0xe273d6,A2          ; blink state struct
 *   00e6ff72    tst.b (A2)                    ; check smd_time_com
 *   00e6ff74    bpl.b 0x00e6ffa0              ; if not active, skip blink
 *   00e6ff76    tst.w (0x4,A2)                ; blink_counter
 *   00e6ff7a    bne.b 0x00e6ff9c              ; if counter != 0, skip call
 *   00e6ff7c    move.w (0x1d98,A5),D0w        ; default_unit
 *   00e6ff80    ext.l D0
 *   00e6ff82    lsl.l #0x2,D0                 ; unit * 4
 *   00e6ff84    lea (0x0,A5,D0*0x1),A0
 *   00e6ff88    movea.l (0x1da0,A0),A1        ; blink_func_ptable[unit]
 *   00e6ff8c    jsr (A1)                      ; call blink function
 *   00e6ff8e    tst.b (0x2,A2)                ; blink_flag
 *   00e6ff92    bpl.b 0x00e6ff9c              ; if cursor off, normal rate
 *   00e6ff94    move.l #0x3d090,(-0x8,A6)     ; use slow rate
 *   00e6ff9c    clr.w (0x4,A2)                ; reset counter
 *   00e6ffa0    move.l (-0x8,A6),-(SP)        ; timer interval
 *   00e6ffa4    jsr 0x00e72690.l              ; reschedule timer
 *   00e6ffaa    addq.w #0x4,SP
 *   ; Check blank timeout...
 *   00e6ffac    tst.b (0xdd,A5)               ; blank_pending
 *   00e6ffb0    bmi.b 0x00e6fffe              ; if set, skip
 *   00e6ffb2    tst.b (0xdc,A5)               ; blank_enabled
 *   00e6ffb6    bpl.b 0x00e6fffe              ; if not enabled, skip
 *   00e6ffb8    tst.l (0xd8,A5)               ; blank_timeout
 *   00e6ffbc    beq.b 0x00e6fffe              ; if 0, skip
 *   00e6ffbe    move.l (0xd8,A5),D0           ; timeout value
 *   00e6ffc2    add.l (0xc8,A5),D0            ; + blank_time
 *   00e6ffc6    cmp.l (0x00e2b0d4).l,D0       ; compare to TIME_$CLOCKH
 *   00e6ffcc    bge.b 0x00e6fffe              ; if not expired, skip
 *   00e6ffce    tst.l (0xc8,A5)               ; blank_time
 *   00e6ffd2    beq.b 0x00e6fff6              ; if 0, set initial time
 *   ; Blank timeout expired - turn off video
 *   00e6ffd4    move.w (0x00e2060a).l,D0w     ; PROC1_$AS_ID
 *   00e6ffda    add.w D0w,D0w
 *   00e6ffdc    move.w (0x1d98,A5),(0x48,A5,D0w*0x1) ; save unit for ASID
 *   00e6ffe2    pea (-0x4,A6)                 ; &local_status
 *   00e6ffe6    pea (-0x1b8e,PC)              ; video_off flag ptr
 *   00e6ffea    bsr.w 0x00e6f838              ; VIDEO_CTL
 *   00e6ffee    addq.w #0x8,SP
 *   00e6fff0    st (0xdd,A5)                  ; blank_pending = true
 *   00e6fff4    bra.b 0x00e6fffe
 *   00e6fff6    move.l (0x00e2b0d4).l,(0xc8,A5) ; blank_time = TIME_$CLOCKH
 *   00e6fffe    tst.w (0xe2,A5)               ; tp_cursor_timeout
 *   00e70002    bmi.b 0x00e70018              ; if negative, skip
 *   00e70004    addq.w #0x1,(0xe2,A5)         ; timeout++
 *   00e70008    cmpi.w #0x2,(0xe2,A5)         ; if timeout >= 2
 *   00e7000e    blt.b 0x00e70018              ; if not, skip
 *   00e70010    pea (0x1d98,A5)               ; &default_unit
 *   00e70014    bsr.w 0x00e6eace              ; STOP_TP_CURSOR
 *   00e70018    movem.l (-0x18,A6),{  A2 A5}
 *   00e7001e    unlk A6
 *   00e70020    rts
 */
void SMD_$BLINK_CURSOR_CALLBACK(void)
{
    uint32_t interval;
    status_$t local_status;

    interval = BLINK_INTERVAL_NORMAL;

    /* Check if cursor blink is active */
    if (SMD_BLINK_STATE.smd_time_com < 0) {
        /* Counter check - only blink when counter is 0 */
        if (SMD_BLINK_STATE.blink_counter == 0) {
            /* Call the unit-specific blink function */
            /* The blink function pointer table is at A5+0x1DA0 for each unit */
            SMD_BLINK_FUNC_PTABLE[SMD_DEFAULT_DISPLAY_UNIT]();

            /* If cursor is currently visible, use slower blink rate */
            if (SMD_BLINK_STATE.blink_flag < 0) {
                interval = BLINK_INTERVAL_SLOW;
            }
        }
        /* Reset blink counter */
        SMD_BLINK_STATE.blink_counter = 0;
    }

    /* Reschedule the timer callback */
    smd_$reschedule_blink_timer(interval);

    /* Check display blank timeout */
    if (SMD_GLOBALS.blank_pending >= 0 &&
        SMD_GLOBALS.blank_enabled < 0 &&
        SMD_GLOBALS.blank_timeout != 0) {

        uint32_t expire_time = SMD_GLOBALS.blank_timeout + SMD_GLOBALS.blank_time;

        if (expire_time < TIME_$CLOCKH) {
            if (SMD_GLOBALS.blank_time == 0) {
                /* Initialize blank time */
                SMD_GLOBALS.blank_time = TIME_$CLOCKH;
            } else {
                /* Blank timeout expired - turn off video */
                SMD_GLOBALS.asid_to_unit[PROC1_$AS_ID] = SMD_DEFAULT_DISPLAY_UNIT;

                /* Turn off video */
                static uint8_t video_off_flag = 0;  /* SMD_VIDEO_DISABLE */
                SMD_$VIDEO_CTL(&video_off_flag, &local_status);

                SMD_GLOBALS.blank_pending = 0xFF;
            }
        }
    }

    /* Check trackpad cursor timeout */
    if (SMD_GLOBALS.tp_cursor_timeout >= 0) {
        SMD_GLOBALS.tp_cursor_timeout++;
        if (SMD_GLOBALS.tp_cursor_timeout >= 2) {
            SMD_$STOP_TP_CURSOR(&SMD_DEFAULT_DISPLAY_UNIT);
        }
    }
}
