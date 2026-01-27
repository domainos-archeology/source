/*
 * KBD_$INIT - Initialize keyboard state structure
 *
 * Initializes a keyboard state structure for a terminal line.
 * Sets up the touchpad buffer pointer, event counter, and default values.
 *
 * Parameters:
 *   state - Pointer to keyboard state structure to initialize
 *
 * Original address: 0x00e33364
 */

#include "kbd/kbd_internal.h"

/* Default keyboard type string at 0xe333da */
static uint8_t default_kbd_type[] = { 0x00 };

void KBD_$INIT(kbd_state_t *state)
{
    /* Clear touchpad buffer head and tail indices */
    TERM_$TPAD_BUFFER.head = 0;
    TERM_$TPAD_BUFFER.tail = 0;

    /* Initialize state fields */
    state->state = 0;
    state->sub_state = 0;
    *(uint16_t *)((uint8_t *)state + 0x3e) = 0;
    state->flags = 0;

    /* Set keyboard type to default */
    kbd_$set_type(state, default_kbd_type, 1);

    /* Set touchpad buffer pointer */
    state->tpad_buffer = &TERM_$TPAD_BUFFER;

    /* Initialize event counter */
    EC_$INIT(&state->ec);

    /* Set ring buffer indices */
    state->ring_head = 0x10001;
    state->ring_tail = 0x40;

    /* Set secondary values */
    state->flags2 = 0x10001;
    state->value2 = 0x40;
}
