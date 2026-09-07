/*
 * smd/reset_display_globals.c - smd_$reset_display_globals implementation
 *
 * Resets the module-wide cursor, tracking and event-queue state after a
 * display has been (re)associated with a process.
 *
 * Original address: 0x00E6D7E2 (renamed from FUN_00e6d7e2)
 *
 * NOTE: this routine never loads A5 itself - it uses the caller's A5, which
 * every caller has already set to 0x00E82B8C (&SMD_GLOBALS).  Callers:
 * SMD_$ASSOC (0x00E6D91C), SMD_$RETURN_DISPLAY (0x00E6F794) and SMD_$BORROW_DISPLAY (0x00E6F6BA);
 * all pass (unit, true).
 *
 * Assembly (every instruction accounted for):
 *   00e6d7e2    link.w A6,-0xc
 *   00e6d7e6    movem.l {  A2 D3 D2},-(SP)
 *   00e6d7ea    move.w (0x8,A6),D0w         ; D0 = unit
 *   00e6d7ee    move.b (0xa,A6),D1b         ; D1 = `full` boolean
 *   00e6d7f2    move.w D0w,D2w
 *   00e6d7f4    movea.l #0xe27376,A0        ; display info base
 *   00e6d7fa    ext.l D2
 *   00e6d7fc    movea.l #0xe273d6,A1        ; &SMD_BLINK_STATE
 *   00e6d802    lsl.l #0x5,D2               ; unit * 32
 *   00e6d804    move.l D2,D3
 *   00e6d806    add.l D3,D3                 ; unit * 64
 *   00e6d808    add.l D3,D2                 ; unit * 96 (0x60)
 *   00e6d80a    lea (0x0,A0,D2*0x1),A0      ; A0 = base + unit*0x60
 *   00e6d80e    clr.l (-0x2e,A0)            ; info[unit-1].kbd_cursor_pos = 0
 *   00e6d812    clr.w (-0x2a,A0)            ; info[unit-1].field_36 = 0
 *   00e6d816    clr.b (-0x28,A0)            ; info[unit-1].kbd_cursor_type = 0
 *   00e6d81a    clr.b (A1)                  ; SMD_BLINK_STATE.smd_time_com = 0
 *   00e6d81c    clr.b (0x2,A1)              ; SMD_BLINK_STATE.blink_flag = 0
 *   00e6d820    tst.b D1b
 *   00e6d822    bpl.b 0x00e6d844            ; `full` false -> skip
 *   00e6d824    clr.w (0xcc,A5)             ; saved_cursor_pos, high half
 *   00e6d828    clr.w (0xce,A5)             ; saved_cursor_pos, low half
 *   00e6d82c    clr.w (0xd6,A5)             ; last_button_state = 0
 *   00e6d830    clr.w (0x728,A5)            ; event_queue_head = 0
 *   00e6d834    clr.w (0x72a,A5)            ; event_queue_tail = 0
 *   00e6d838    st (0x173e,A5)              ; field_173e = true
 *   00e6d83c    st (0x173f,A5)              ; field_173f = true
 *   00e6d840    clr.b (0x173d,A5)           ; field_173d = false
 *   00e6d844    clr.b (0xdc,A5)             ; blank_enabled = false
 *   00e6d848    clr.b (0xdd,A5)             ; blank_pending = false
 *   00e6d84c    move.w #-0x1,(0xe2,A5)      ; tp_cursor_timeout = -1
 *   00e6d852    move.w #0x1,(0xde,A5)       ; tp_reporting = 1
 *   00e6d858    clr.b (0xe0,A5)             ; tracking_enabled = false
 *   00e6d85c    move.w #0x1,(0xe6,A5)       ; tracking_rect_count = 1
 *   00e6d862    lea (0xc0,A5),A2            ; &kbd_cursor_track_rect
 *   00e6d866    move.l (A2)+,(0xe8,A5)      ; tracking_rects[0], first 4 bytes
 *   00e6d86a    move.l (A2)+,(0xec,A5)      ; tracking_rects[0], last 4 bytes
 *   00e6d86e    move.w #0x2,(0xe4,A5)       ; tracking_cursor_num = 2
 *   00e6d874    clr.b (0x1744,A5)           ; cursor_pending_flag = false
 *   00e6d878    movem.l (-0x18,A6),{  D2 D3 A2}
 *   00e6d87e    unlk A6
 *   00e6d880    rts
 */

#include "smd/smd_internal.h"

/*
 * smd_$reset_display_globals - Reset the module-wide cursor/tracking state.
 *
 * Parameters:
 *   unit - display unit number (1-based)
 *   full - Domain boolean; when true the locator and event-queue state is
 *          reset as well
 */
void smd_$reset_display_globals(int16_t unit, boolean full)
{
    smd_display_info_t *info;

    /* 0x00e6d7f4-0x00e6d816: the info table is 1-based on the unit number. */
    info = smd_$unit_info(unit);
    info->kbd_cursor_pos = 0;
    info->field_36 = 0;
    info->kbd_cursor_type = 0;

    /* 0x00e6d81a-0x00e6d81c */
    SMD_BLINK_STATE.smd_time_com = 0;
    SMD_BLINK_STATE.blink_flag = 0;

    /* 0x00e6d820: `full` is a Domain boolean, tested with tst.b/bpl. */
    if (full < 0) {
        /* 0x00e6d824-0x00e6d828: the original clears the two halves of the
         * saved locator position separately. */
        SMD_GLOBALS.saved_cursor_pos = 0;
        SMD_GLOBALS.last_button_state = 0;
        SMD_GLOBALS.event_queue_head = 0;
        SMD_GLOBALS.event_queue_tail = 0;
        SMD_GLOBALS.field_173e = (boolean)0xFF;
        SMD_GLOBALS.field_173f = (boolean)0xFF;
        SMD_GLOBALS.field_173d = 0;
    }

    /* 0x00e6d844-0x00e6d874 */
    SMD_GLOBALS.blank_enabled = 0;
    SMD_GLOBALS.blank_pending = 0;
    SMD_GLOBALS.tp_cursor_timeout = -1;
    SMD_GLOBALS.tp_reporting = 1;
    SMD_GLOBALS.tracking_enabled = 0;
    SMD_GLOBALS.tracking_rect_count = 1;
    SMD_GLOBALS.tracking_rects[0] = SMD_GLOBALS.kbd_cursor_track_rect;
    SMD_GLOBALS.tracking_cursor_num = 2;
    SMD_GLOBALS.cursor_pending_flag = 0;
}
