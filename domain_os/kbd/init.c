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

    /*
     * 00e333b6 `move.l #0x10001,(0x58,A2)` seeds the head and tail words at
     * 0x58/0x5A in one longword store (both = 1, the ring is 1-based), and
     * 00e333be `move.w #0x40,(0x5c,A2)` sets the capacity word at 0x5C.
     */
    state->ring_head = 1;
    state->ring_tail = 1;
    state->ring_size = KBD_RING_SIZE;

    /* Set secondary values */
    state->flags2 = 0x10001;
    state->value2 = 0x40;
}
