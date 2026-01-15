/*
 * KBD_$OUTPUT_BUFFER_DRAINED - Signal output buffer drained
 *
 * Advances the keyboard event counter to signal that the output
 * buffer has been drained.
 *
 * Parameters:
 *   state - Pointer to keyboard state structure
 *
 * Original address: 0x00e1ce96
 */

#include "kbd/kbd_internal.h"

void KBD_$OUTPUT_BUFFER_DRAINED(kbd_state_t *state)
{
    EC_$ADVANCE_WITHOUT_DISPATCH(&state->ec);
}
