/*
 * smd/get_unit_event.c - SMD_$GET_UNIT_EVENT implementation
 *
 * Retrieves the next event from the SMD event queue for the current
 * display unit.
 *
 * Original address: 0x00E6EEA8
 */

#include "smd/smd_internal.h"
#include "ml/ml.h"
#include "time/time.h"

/*
 * Event data structure returned to caller.
 * Contains timestamp, position, and event-specific data.
 * Size: 14 bytes
 */
typedef struct smd_event_data_t {
    uint32_t    timestamp;              /* 0x00: Event timestamp (clock_t) */
    uint32_t    field_04;               /* 0x04: Unknown field */
    uint16_t    field_08;               /* 0x08: Unknown field */
    uint16_t    unit;                   /* 0x0A: Display unit */
    uint16_t    button_or_char;         /* 0x0C: Button state or character */
} smd_event_data_t;

/*
 * SMD_$GET_UNIT_EVENT - Get next event from queue
 *
 * Retrieves the next pending event from the SMD event queue.
 * Events include button presses/releases, keystrokes, and pointer movements.
 *
 * Parameters:
 *   event_type  - Output: receives the event type code
 *   event_data  - Output: receives event data (timestamp, position, etc.)
 *   status_ret  - Output: receives status code
 *
 * Event types returned:
 *   0 = No event (queue empty)
 *   1 = Button down
 *   2 = Button up
 *   3 = Keystroke
 *   4 = Special event
 *   5 = Pointer up
 */
void SMD_$GET_UNIT_EVENT(uint16_t *event_type, smd_event_data_t *event_data,
                         status_$t *status_ret)
{
    smd_event_entry_t *entry;
    uint16_t tail;
    uint16_t type;
    uint16_t result_type;
    uint16_t button_data;

    /* Clear status and event type */
    *status_ret = 0;
    *event_type = SMD_EVTYPE_NONE;

    /* Acquire lock for queue access */
    ML_$LOCK(SMD_REQUEST_LOCK);

    /* Poll keyboard for any pending input */
    smd_$poll_keyboard();

    /* Get current tail index and calculate entry offset */
    tail = SMD_GLOBALS.event_queue_tail;

    /* Check if queue is empty */
    if (SMD_GLOBALS.event_queue_head == tail) {
        /* Queue empty - release lock and return */
        ML_$UNLOCK(SMD_REQUEST_LOCK);
        SMD_GLOBALS.blank_enabled = 0xFF;  /* Re-enable blanking */
        return;
    }

    /* Get pointer to current entry */
    entry = &SMD_GLOBALS.event_queue[tail];

    /* Read event data from queue entry */
    event_data->timestamp = entry->timestamp;
    event_data->field_04 = *(uint32_t *)&entry->field_08;  /* Copy next 4 bytes */
    event_data->field_08 = entry->unit;
    event_data->unit = entry->unit;

    /* Convert internal event type to public type */
    type = entry->event_type;

    switch (type) {
    case SMD_EVTYPE_INT_KEY_META0:      /* 0x00 - key with meta */
    case SMD_EVTYPE_INT_KEY_META:       /* 0x07 - key with meta */
        result_type = SMD_EVTYPE_KEYSTROKE;
        /* For key events, extract character and pack with high byte cleared */
        button_data = (uint16_t)((uint8_t)entry->button_or_char) << 8;
        break;

    case SMD_EVTYPE_INT_BUTTON_DOWN:    /* 0x08 - button down */
    case SMD_EVTYPE_INT_BUTTON_DOWN2:   /* 0x0D - button down variant */
        result_type = SMD_EVTYPE_BUTTON_DOWN;
        button_data = entry->button_or_char;
        break;

    case SMD_EVTYPE_INT_SPECIAL:        /* 0x0B - special */
        result_type = SMD_EVTYPE_SPECIAL;
        break;

    case SMD_EVTYPE_INT_KEY_NORMAL:     /* 0x0C - normal keystroke */
        result_type = SMD_EVTYPE_KEYSTROKE;
        /* Pack both character bytes */
        button_data = ((uint8_t)(entry->button_or_char >> 8) << 8) |
                      ((uint8_t)(entry->button_or_char & 0xFF));
        break;

    case SMD_EVTYPE_INT_BUTTON_UP:      /* 0x0E - button up */
        result_type = SMD_EVTYPE_BUTTON_UP;
        button_data = entry->button_or_char;
        break;

    case SMD_EVTYPE_INT_POINTER_UP:     /* 0x0F - pointer up */
        result_type = SMD_EVTYPE_POINTER_UP;
        button_data = entry->button_or_char;
        break;

    default:
        /* Unknown type - pass through */
        result_type = type;
        button_data = entry->button_or_char;
        break;
    }

    /* Advance tail pointer (circular buffer) */
    SMD_GLOBALS.event_queue_tail = (tail + 1) & SMD_EVENT_QUEUE_MASK;

    /* Release lock */
    ML_$UNLOCK(SMD_REQUEST_LOCK);

    /* Unblank display on user activity */
    SMD_$UNBLANK();

    /* Copy remaining event data */
    event_data->button_or_char = button_data;

    /* Return event type */
    *event_type = result_type;

    /* Re-enable blanking */
    SMD_GLOBALS.blank_enabled = 0xFF;
}
