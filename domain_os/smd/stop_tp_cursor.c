/*
 * smd/stop_tp_cursor.c - SMD_$STOP_TP_CURSOR implementation
 *
 * Stops trackpad cursor tracking and sends a final location event.
 *
 * Original address: 0x00E6EACE
 */

#include "smd/smd_internal.h"
#include "ml/ml.h"

/* Lock data addresses from original code */
static const uint32_t stop_tp_cursor_lock_data_1 = 0x00E6D92C;
static const uint32_t stop_tp_cursor_lock_data_2 = 0x00E6E458;

/*
 * SMD_$STOP_TP_CURSOR - Stop trackpad cursor
 *
 * Stops trackpad cursor tracking. Disables the cursor tracking
 * timeout counter, sends a final location event (type 0x0B),
 * and optionally shows the cursor at a default position.
 *
 * Parameters:
 *   unit - Pointer to display unit number
 *
 * Original address: 0x00E6EACE
 *
 * Assembly:
 *   00e6eace    link.w A6,-0x4
 *   00e6ead2    pea (A5)
 *   00e6ead4    lea (0xe82b8c).l,A5
 *   00e6eada    movea.l (0x8,A6),A0
 *   00e6eade    move.w (A0),(-0x4,A6)         ; local_unit = *unit
 *   00e6eae2    move.w #-0x1,(0xe2,A5)        ; tp_cursor_timeout = -1
 *   00e6eae8    subq.l #0x2,SP
 *   00e6eaea    move.w #0x8,-(SP)             ; lock_id = 8
 *   00e6eaee    jsr 0x00e20b12.l              ; ML_$LOCK(smd_$request_lock)
 *   00e6eaf4    addq.w #0x4,SP
 *   00e6eaf6    bsr.w 0x00e6e84c              ; smd_$poll_keyboard
 *   00e6eafa    tst.b D0b
 *   00e6eafc    bpl.b 0x00e6eb16              ; if not active, skip
 *   00e6eafe    subq.l #0x2,SP
 *   00e6eb00    clr.w -(SP)                   ; buttons = 0
 *   00e6eb02    move.l (0xcc,A5),-(SP)        ; saved_cursor_pos
 *   00e6eb06    move.w #0xb,-(SP)             ; event_type = 0x0B (stop)
 *   00e6eb0a    move.w (-0x4,A6),-(SP)        ; unit
 *   00e6eb0e    bsr.w 0x00e6e8d6              ; smd_$send_loc_event
 *   00e6eb12    lea (0xc,SP),SP
 *   00e6eb16    subq.l #0x2,SP
 *   00e6eb18    move.w #0x8,-(SP)             ; lock_id = 8
 *   00e6eb1c    jsr 0x00e20b62.l              ; ML_$UNLOCK(smd_$request_lock)
 *   00e6eb22    addq.w #0x4,SP
 *   00e6eb24    tst.b (0xe0,A5)               ; tp_cursor_active
 *   00e6eb28    bpl.b 0x00e6eb3a              ; if not active, exit
 *   00e6eb2a    pea (-0x6d4,PC)               ; lock_data_2
 *   00e6eb2e    pea (-0x1204,PC)              ; lock_data_1
 *   00e6eb32    pea (0x1d94,A5)               ; &default_cursor_pos
 *   00e6eb36    bsr.w 0x00e6e1cc              ; SHOW_CURSOR
 *   00e6eb3a    movea.l (-0x8,A6),A5
 *   00e6eb3e    unlk A6
 *   00e6eb40    rts
 */
void SMD_$STOP_TP_CURSOR(uint16_t *unit)
{
    uint16_t local_unit;
    int8_t active;

    local_unit = *unit;

    /* Disable tracking timeout counter */
    SMD_GLOBALS.tp_cursor_timeout = 0xFFFF;

    /* Lock the request lock */
    ML_$LOCK(SMD_REQUEST_LOCK);

    /* Check if trackpad cursor is active */
    active = smd_$poll_keyboard();
    if (active < 0) {
        /* Send stop event (type 0x0B) */
        smd_$send_loc_event(local_unit, 0x0B, SMD_GLOBALS.saved_cursor_pos, 0);
    }

    /* Unlock */
    ML_$UNLOCK(SMD_REQUEST_LOCK);

    /* If cursor tracking was active, show cursor at default position */
    if (SMD_GLOBALS.tp_cursor_active < 0) {
        SHOW_CURSOR(&SMD_GLOBALS.cursor_pos_sentinel,
                    (int16_t *)&stop_tp_cursor_lock_data_1,
                    (int8_t *)&stop_tp_cursor_lock_data_2);
    }
}
