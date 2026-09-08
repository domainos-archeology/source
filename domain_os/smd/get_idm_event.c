/*
 * smd/get_idm_event.c - SMD_$GET_IDM_EVENT implementation
 *
 * Retrieves the next event from the SMD event queue, converting
 * the data format for IDM (Input Device Manager) consumers.
 *
 * Original address: 0x00E6EE28
 */

#include "smd/smd_internal.h"

/* smd_unit_event_t and smd_idm_event_t are now defined in smd_internal.h */

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

    /* Copy base event data (first 10 bytes).  smd_unit_event_t's first
     * longword is the packed cursor position and its second is the
     * timestamp; smd_idm_event_t's field names still carry the old,
     * one-longword-shifted spelling (bead source-v5vu renamed only the unit
     * record, which is the one SMD_$GET_UNIT_EVENT fills). */
    idm_data->timestamp = unit_event.pos;
    idm_data->field_04 = unit_event.timestamp;
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
        /*
         * 0x00E6EE92  move.b (-0x4,A6),(0xa,A3)
         * 0x00E6EE98  move.b (-0x3,A6),(0xb,A3)
         *
         * unit_event sits at (-0x10,A6), so -0x4 and -0x3 are its 0x0C and
         * 0x0D bytes - the two halves of button_or_char - and they land at
         * idm_data 0x0A and 0x0B in that same order.  The pair of byte moves
         * is a plain word copy; it does NOT swap the character and modifier.
         */
        idm_data->data = unit_event.button_or_char;
        break;

    case SMD_EVTYPE_POINTER_UP:         /* 5 */
        /* Convert pointer up to button down using saved state */
        *event_type = SMD_EVTYPE_BUTTON_DOWN;
        idm_data->data = SMD_GLOBALS.last_idm_button;
        break;

    default:
        /*
         * The dispatch is "subq.w #0x1,D0w / cmpi.w #0x5,D0w / bcc" over the
         * word table at 0x00E6EE6E (00 16 00 16 00 24 00 30 00 0a), so type 4
         * selects 0x00E6EE6E + 0x30 = 0x00E6EE9E - the epilogue - and every
         * type outside 1..5 falls straight through to it.  Neither writes
         * idm_data->data: the 10-byte copy above covers only 0x00..0x09, so
         * the caller's record keeps whatever was already in its 0x0A word.
         */
        break;
    }
}
