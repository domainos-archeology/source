/*
 * smd/get_idm_event.c - SMD_$GET_IDM_EVENT implementation
 *
 * Retrieves the next event from the SMD event queue, converting
 * the data format for IDM (Input Device Manager) consumers.
 *
 * Original address: 0x00E6EE28
 */

#include "smd/smd_internal.h"

/*
 * Event data returned by SMD_$GET_UNIT_EVENT (14 bytes)
 */
typedef struct smd_unit_event_t {
    uint32_t    timestamp;              /* 0x00: Event timestamp */
    uint32_t    field_04;               /* 0x04: Unknown */
    uint16_t    field_08;               /* 0x08: Unknown */
    uint16_t    unit;                   /* 0x0A: Display unit */
    uint16_t    button_or_char;         /* 0x0C: Button state or character */
} smd_unit_event_t;

/*
 * IDM event data structure (12 bytes)
 */
typedef struct smd_idm_event_t {
    uint32_t    timestamp;              /* 0x00: Event timestamp */
    uint32_t    field_04;               /* 0x04: Unknown */
    uint16_t    field_08;               /* 0x08: Unknown */
    uint16_t    data;                   /* 0x0A: Event-specific data */
} smd_idm_event_t;

/*
 * SMD_$GET_IDM_EVENT - Get next IDM event
 *
 * Wrapper around SMD_$GET_UNIT_EVENT that reformats event data
 * for IDM consumers. Handles button state tracking and keystroke
 * character/modifier packing.
 *
 * Parameters:
 *   event_type  - Output: receives the event type code
 *   idm_data    - Output: receives formatted IDM event data
 *   status_ret  - Output: receives status code
 *
 * Event types:
 *   1 = Button down (data = button state, saved for pointer up)
 *   2 = Button up (data = button state)
 *   3 = Keystroke (data[0] = char, data[1] = modifier)
 *   5 = Pointer up (converted to button down with saved state)
 */
void SMD_$GET_IDM_EVENT(uint16_t *event_type, smd_idm_event_t *idm_data,
                        status_$t *status_ret)
{
    smd_unit_event_t unit_event;
    uint16_t type;

    /* Get event from underlying queue */
    SMD_$GET_UNIT_EVENT(event_type, (void *)&unit_event, status_ret);

    /* Copy base event data (first 10 bytes) */
    idm_data->timestamp = unit_event.timestamp;
    idm_data->field_04 = unit_event.field_04;
    idm_data->field_08 = unit_event.field_08;

    /* Handle event type-specific data conversion */
    type = *event_type;

    switch (type) {
    case SMD_EVTYPE_BUTTON_DOWN:        /* 1 */
    case SMD_EVTYPE_BUTTON_UP:          /* 2 */
        /* Save button state for pointer up events */
        SMD_GLOBALS.last_idm_button = unit_event.button_or_char;
        idm_data->data = unit_event.button_or_char;
        break;

    case SMD_EVTYPE_KEYSTROKE:          /* 3 */
        /* Unpack character and modifier bytes:
         * unit_event has char in high byte, modifier in low byte
         * IDM wants: data[0] = char, data[1] = modifier */
        idm_data->data = ((unit_event.button_or_char >> 8) & 0xFF) |
                         ((unit_event.button_or_char & 0xFF) << 8);
        break;

    case SMD_EVTYPE_POINTER_UP:         /* 5 */
        /* Convert pointer up to button down using saved state */
        *event_type = SMD_EVTYPE_BUTTON_DOWN;
        idm_data->data = SMD_GLOBALS.last_idm_button;
        break;

    default:
        /* Other types - pass through */
        idm_data->data = unit_event.button_or_char;
        break;
    }
}
