/*
 * smd/loc_event.c - SMD_$LOC_EVENT implementation
 *
 * Handles location (mouse/pointer) events, managing the event queue
 * and cursor position updates.
 *
 * Original address: 0x00E6E9A0
 */

#include "smd/smd_internal.h"
#include "ml/ml.h"
#include "math/math.h"

/*
 * SMD_$LOC_EVENT - Process location event
 *
 * Called by the input system when pointer/mouse location changes.
 * Generates appropriate events (button down/up, pointer moves) and
 * updates cursor state. Handles coalescing of move events when possible.
 *
 * Parameters:
 *   button_state - Button pressed flags (negative = buttons pressed)
 *   unit         - Display unit number
 *   pos          - Cursor position (packed as smd_cursor_pos_t)
 *   buttons      - Current button state value
 *
 * Returns:
 *   Result flags (negative if cursor should be updated)
 */
int8_t SMD_$LOC_EVENT(int8_t button_state, int16_t unit, uint32_t pos, int16_t buttons)
{
    int8_t poll_result;
    int8_t pos_changed;
    int16_t event_type;
    int16_t prev_idx;
    int entry_offset;
    smd_event_entry_t *entry;
    int8_t coalesced;
    int8_t result;

    /* Acquire lock for queue access */
    ML_$LOCK(SMD_REQUEST_LOCK);

    /* Poll keyboard for any pending input */
    poll_result = smd_$poll_keyboard();
    if (poll_result >= 0) {
        /* No keyboard input pending, check position */
        result = 0;
        goto done;
    }

    /* Check if position changed from last event */
    pos_changed = -(SMD_GLOBALS.saved_cursor_pos.x != (int16_t)pos ||
                    SMD_GLOBALS.saved_cursor_pos.y != (int16_t)(pos >> 16));

    if (pos_changed < 0) {
        /* Position changed - determine event type */
        if (button_state < 0) {
            /* Buttons pressed - pointer up event (0x0F) */
            event_type = SMD_EVTYPE_INT_POINTER_UP;
        } else {
            /* No buttons - button down event (0x0D) */
            event_type = SMD_EVTYPE_INT_BUTTON_DOWN2;
        }

        /* Calculate previous queue entry index */
        prev_idx = M_$OIS_WLW(SMD_GLOBALS.event_queue_head + 0xFF, 0x100);

        /* Check if we can coalesce with previous event */
        coalesced = 0;
        entry_offset = prev_idx * sizeof(smd_event_entry_t);

        if (SMD_GLOBALS.event_queue_head != SMD_GLOBALS.event_queue_tail &&
            SMD_GLOBALS.cursor_tracking_count != 0) {
            entry = &SMD_GLOBALS.event_queue[prev_idx];

            /* Check if same unit */
            if (unit == entry->unit) {
                /* Check if compatible event type */
                if (event_type == entry->event_type) {
                    /* Same type - can coalesce */
                    entry->button_or_char = buttons;
                    entry->pos = *(smd_cursor_pos_t *)&pos;
                    coalesced = -1;
                } else if (button_state < 0 &&
                           entry->event_type == SMD_EVTYPE_INT_BUTTON_DOWN2) {
                    /* Upgrade button down to pointer up */
                    entry->event_type = SMD_EVTYPE_INT_POINTER_UP;
                    entry->button_or_char = buttons;
                    entry->pos = *(smd_cursor_pos_t *)&pos;
                    coalesced = -1;
                }
            }
        }

        /* If not coalesced, add new event */
        if (coalesced >= 0) {
            smd_$enqueue_event(unit, event_type, pos, buttons);
        }

        /* Clear tracking count flag and update saved position */
        SMD_GLOBALS.tp_cursor_timeout = 0;
        SMD_GLOBALS.saved_cursor_pos = *(smd_cursor_pos_t *)&pos;
    }

    /* Handle button up event if no buttons pressed and button state changed */
    if (button_state >= 0 && buttons != SMD_GLOBALS.last_button_state) {
        smd_$enqueue_event(unit, SMD_EVTYPE_INT_BUTTON_UP, pos, buttons);
        SMD_GLOBALS.last_button_state = buttons;
    }

    result = pos_changed;

done:
    ML_$UNLOCK(SMD_REQUEST_LOCK);

    /* If position changed and cursor active, update cursor display */
    result &= SMD_GLOBALS.tp_cursor_active;
    if (result < 0) {
        result = SHOW_CURSOR(&pos, &SMD_GLOBALS.default_cursor_pos.x,
                             &SMD_GLOBALS.tp_cursor_active);
    }

    return result;
}

/*
 * smd_$poll_keyboard - Poll keyboard for pending input
 *
 * Checks the keyboard for pending characters and adds them to the
 * event queue. Called before reading from the queue.
 *
 * Returns:
 *   Negative (0xFF) if input was available, 0 otherwise
 *
 * Original address: 0x00E6E84C
 */
int8_t smd_$poll_keyboard(void)
{
    int8_t result = 0;
    int8_t kbd_status;
    uint16_t next_head;
    smd_event_entry_t *entry;
    uint8_t char_code;
    uint8_t modifier;
    status_$t status;

    /* Check if keyboard has data for current unit */
    kbd_status = smd_$validate_unit(SMD_GLOBALS.default_unit);
    if (kbd_status < 0) {
        result = 0xFF;

        /* Loop while we can add entries to the queue */
        while (1) {
            next_head = (SMD_GLOBALS.event_queue_head + 1) & SMD_EVENT_QUEUE_MASK;
            if (next_head == SMD_GLOBALS.event_queue_tail) {
                /* Queue full */
                break;
            }

            /* Get entry at current head */
            entry = &SMD_GLOBALS.event_queue[SMD_GLOBALS.event_queue_head];

            /* Try to get character from keyboard */
            if (KBD_$GET_CHAR_AND_MODE(&SMD_ACQ_LOCK_DATA,
                                        &char_code, &modifier, &status) >= 0) {
                /* No more characters */
                break;
            }

            /* Fill in event entry */
            entry->unit = SMD_GLOBALS.default_unit;
            TIME_$CLOCK((clock_t *)&entry->timestamp);

            if (modifier == 0) {
                /* No modifier - simple key event */
                entry->event_type = SMD_EVTYPE_INT_KEY_META0;
            } else {
                /* Has modifier */
                entry->event_type = SMD_EVTYPE_INT_KEY_NORMAL;
            }

            /* Store character and modifier */
            entry->button_or_char = ((uint16_t)char_code << 8) | modifier;

            /* Advance head pointer */
            SMD_GLOBALS.event_queue_head = next_head;
        }
    }

    return result;
}

/*
 * smd_$enqueue_event - Add event to queue
 *
 * Adds a location/input event to the circular event queue.
 * Timestamps the event and signals the display event count.
 *
 * Parameters:
 *   unit    - Display unit number
 *   type    - Internal event type code
 *   pos     - Cursor position (packed as uint32_t)
 *   buttons - Button state or character value
 *
 * Original address: 0x00E6E8D6
 */
void smd_$enqueue_event(uint16_t unit, uint16_t type, uint32_t pos, uint16_t buttons)
{
    uint16_t next_head;
    smd_event_entry_t *entry;

    /* Calculate next head position */
    next_head = (SMD_GLOBALS.event_queue_head + 1) & SMD_EVENT_QUEUE_MASK;

    /* Check if queue has room */
    if (next_head == SMD_GLOBALS.event_queue_tail) {
        /* Queue full - drop event */
        return;
    }

    /* Get entry at current head */
    entry = &SMD_GLOBALS.event_queue[SMD_GLOBALS.event_queue_head];

    /* Fill in event data */
    TIME_$CLOCK((clock_t *)&entry->timestamp);
    entry->pos = *(smd_cursor_pos_t *)&pos;
    entry->event_type = type;
    entry->button_or_char = buttons;
    entry->unit = unit;

    /* Advance head pointer */
    SMD_GLOBALS.event_queue_head = next_head;

    /* Signal event count to wake any waiters */
    EC_$ADVANCE(&DTTE);
}
