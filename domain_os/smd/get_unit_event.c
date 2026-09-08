/*
 * smd/get_unit_event.c - SMD_$GET_UNIT_EVENT (0x00E6EEA8, 236 bytes)
 *
 * Retrieve the next event from the SMD event queue.
 *
 * Frame: `link.w A6,-0x18` / `movem.l {A5 A2 D2},-(SP)`;
 *        `lea (0xe82b8c).l,A5` makes A5 = &SMD_GLOBALS.
 *   +0x08 event_type  longword (A2)
 *   +0x0C event_data  longword (the 14-byte reply buffer)
 *   +0x10 status_ret  longword (A0, cleared at 0x00E6EEBE)
 *
 * The routine builds the reply in a 14-byte local at (-0x10,A6) and copies
 * it out with three `move.l (A0)+,(A1)+` and one `move.w (A0)+,(A1)+`
 * (0x00E6EF8E-0x00E6EF94), then stores the result type from D2.
 */

#include "smd/smd_internal.h"
#include "ml/ml.h"
#include "time/time.h"

/*
 * SMD_$GET_UNIT_EVENT - Get next event from queue
 *
 * Parameters:
 *   event_type  - Output: the public event type code (0 when the queue is empty)
 *   event_data  - Output: the 14-byte event record
 *   status_ret  - Output: status code (always 0 in this routine)
 */
void SMD_$GET_UNIT_EVENT(uint16_t *event_type, smd_event_data_t *event_data,
                         status_$t *status_ret)
{
    smd_event_entry_t *entry;
    smd_unit_event_t reply;   /* the local record at (-0x10,A6) */
    uint16_t tail;
    uint16_t type;

    /*
     * D2, the result type.  The jump table at 0x00E6EF16 sends several
     * internal types straight to 0x00E6EF68 without ever loading D2, so on
     * those arms the value stored by `move.w D2w,(A2)` at 0x00E6EF96 is
     * whatever the caller left in D2 - see the switch below.  It is left
     * uninitialised here on purpose; do not invent a value for it.  (A
     * -Wall build flags the read; the kernel build does not enable -Wall,
     * and an initialiser here would be a behaviour change, not a fix.)
     */
    uint16_t result_type;

    /* 0x00E6EEBE clr.l (A0) / 0x00E6EEC0 clr.w (A2) */
    *status_ret = status_$ok;
    *event_type = SMD_EVTYPE_NONE;

    /* 0x00E6EEC2-0x00E6EECE ML_$LOCK(8) */
    ML_$LOCK(SMD_REQUEST_LOCK);

    /* 0x00E6EED0 bsr.w 0x00E6E84C */
    smd_$poll_keyboard();

    /* 0x00E6EED4-0x00E6EEE6: A0 = A5 + tail*0x10, then head == tail? */
    tail = SMD_GLOBALS.event_queue_tail;
    if (SMD_GLOBALS.event_queue_head == tail) {
        /* 0x00E6EF9A: empty queue - unlock and leave *event_type at 0 */
        ML_$UNLOCK(SMD_REQUEST_LOCK);
        SMD_GLOBALS.blank_enabled = (boolean)0xFF;   /* 0x00E6EFA6 st (0xdc,A5) */
        return;
    }

    entry = &SMD_GLOBALS.event_queue[tail];

    /*
     * 0x00E6EEEA-0x00E6EEFC: the reply's first four fields are the entry's
     * first four fields, at the SAME offsets:
     *
     *   0x00E6EEEA  move.l (0x72c,A0),(-0x10,A6)   entry+0x00 -> reply+0x00
     *   0x00E6EEF0  move.l (0x730,A0),(-0xc,A6)    entry+0x04 -> reply+0x04
     *   0x00E6EEF6  move.w (0x734,A0),(-0x8,A6)    entry+0x08 -> reply+0x08
     *   0x00E6EEFC  move.w (0x736,A0),(-0x6,A6)    entry+0x0A -> reply+0x0A
     *
     * (bead source-v5vu: the C used to start from entry+0x04, read field_08
     * as a longword and store the unit twice, shifting the whole reply by
     * one longword.)
     */
    reply.pos = entry->pos;
    reply.timestamp = entry->timestamp;
    reply.field_08 = entry->field_08;
    reply.unit = entry->unit;

    /* 0x00E6EF02-0x00E6EF12: switch on the internal type at entry+0x0C.
     * `cmpi.w #0x10,D0w / bcc.b 0x00E6EF68` sends anything >= 0x10 to the
     * no-op arm; the 16-entry word table at 0x00E6EF16 is
     *   00 2c 00 52 00 52 00 52 00 52 00 52 00 52 00 2c
     *   00 20 00 52 00 52 00 28 00 3a 00 20 00 24 00 4a
     * i.e. displacements added to 0x00E6EF16. */
    type = entry->event_type;

    switch (type) {
    case SMD_EVTYPE_INT_KEY_META0:      /* 0x00 -> 0x00E6EF42 */
    case SMD_EVTYPE_INT_KEY_META:       /* 0x07 -> 0x00E6EF42 */
        /* moveq #0x3,D2 / move.b (0x73a,A0),(-0x4,A6) / clr.b (-0x3,A6):
         * the character byte alone, low byte cleared. */
        result_type = SMD_EVTYPE_KEYSTROKE;
        reply.button_or_char =
            (uint16_t)((uint16_t)((entry->button_or_char >> 8) & 0xFF) << 8);
        break;

    case SMD_EVTYPE_INT_BUTTON_DOWN:    /* 0x08 -> 0x00E6EF36 */
    case SMD_EVTYPE_INT_BUTTON_DOWN2:   /* 0x0D -> 0x00E6EF36 */
        /* moveq #0x1,D2 / bra 0x00E6EF62 move.w (0x73a,A0),(-0x4,A6) */
        result_type = SMD_EVTYPE_BUTTON_DOWN;
        reply.button_or_char = entry->button_or_char;
        break;

    case SMD_EVTYPE_INT_SPECIAL:        /* 0x0B -> 0x00E6EF3E */
        /* moveq #0x4,D2 / bra 0x00E6EF68 - the reply word is NOT written */
        result_type = SMD_EVTYPE_SPECIAL;
        break;

    case SMD_EVTYPE_INT_KEY_NORMAL:     /* 0x0C -> 0x00E6EF50 */
        /* moveq #0x3,D2 / both bytes copied individually */
        result_type = SMD_EVTYPE_KEYSTROKE;
        reply.button_or_char = entry->button_or_char;
        break;

    case SMD_EVTYPE_INT_BUTTON_UP:      /* 0x0E -> 0x00E6EF3A */
        /* moveq #0x2,D2 / bra 0x00E6EF62 */
        result_type = SMD_EVTYPE_BUTTON_UP;
        reply.button_or_char = entry->button_or_char;
        break;

    case SMD_EVTYPE_INT_POINTER_UP:     /* 0x0F -> 0x00E6EF60 */
        /* moveq #0x5,D2 / falls into 0x00E6EF62 */
        result_type = SMD_EVTYPE_POINTER_UP;
        reply.button_or_char = entry->button_or_char;
        break;

    default:
        /*
         * Internal types 1-6, 9 and 0x0A (table displacement 0x52), and
         * every type >= 0x10 (the `bcc` at 0x00E6EF0A), jump directly to
         * 0x00E6EF68.  Neither D2 nor the reply's last word is written on
         * this path, so the caller receives the leftover D2 as the event
         * type and stale stack bytes as the data word.  That is what the
         * image does; nothing is invented here (bead source-v5vu).
         */
        break;
    }

    /* 0x00E6EF68-0x00E6EF72: advance the tail, wrapping at 256 entries */
    SMD_GLOBALS.event_queue_tail = (uint16_t)((tail + 1) & SMD_EVENT_QUEUE_MASK);

    /* 0x00E6EF76-0x00E6EF82 ML_$UNLOCK(8) */
    ML_$UNLOCK(SMD_REQUEST_LOCK);

    /* 0x00E6EF84 bsr.b 0x00E6EFB4 */
    SMD_$UNBLANK();

    /* 0x00E6EF86-0x00E6EF94: the 14-byte block copy */
    *event_data = reply;

    /* 0x00E6EF96 move.w D2w,(A2) */
    *event_type = result_type;

    /* 0x00E6EFA6 st (0xdc,A5) */
    SMD_GLOBALS.blank_enabled = (boolean)0xFF;
}
