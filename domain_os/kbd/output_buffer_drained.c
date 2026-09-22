/*
 * KBD_$OUTPUT_BUFFER_DRAINED - Output-drained notification for a keyboard
 *
 * Installed in TERM_$DATA.ptr_kbd_drain (+0x8C); advances the descriptor's
 * eventcount without dispatching.
 *
 * Parameters:
 *   state - the descriptor ((0x8,A6))
 *
 * Original address: 0x00e1ce96, 22 bytes
 *
 *   00e1ce9a  movea.l (0x8,A6),A0
 *   00e1ce9e  pea (0x4c,A0)                        ; &state->ec
 *   00e1cea2  jsr EC_$ADVANCE_WITHOUT_DISPATCH     ; args reclaimed by unlk
 */

#include "kbd/kbd_internal.h"

void KBD_$OUTPUT_BUFFER_DRAINED(kbd_state_t *state)
{
    /* 0x00E1CE9E..0x00E1CEA2 */
    EC_$ADVANCE_WITHOUT_DISPATCH(&state->ec);
}
