/*
 * KBD_$INIT - Initialise a keyboard descriptor
 *
 * Clears the touchpad sample buffer's indices and the descriptor's state
 * words, installs the default keyboard type "2", points the descriptor at
 * TERM_$TPAD_BUFFER, initialises its eventcount and seeds the key ring.
 *
 * Parameters:
 *   state - the descriptor ((0x8,A6), A2)
 *
 * Original address: 0x00e33364, 118 bytes (map: I E33364 KBD size 78)
 *
 *   00e3336a  movea.l #0xe2dde4,A0                     ; KBD data segment
 *   00e33374  clr.l (0x58,A0)                          ; 0xE2DE3C: TERM_$TPAD_BUFFER.head/tail
 *   00e33378  clr.w (0x38,A2)                          ; state
 *   00e3337c  clr.l (0x3a,A2)                          ; sub_state AND kbd_type_idx
 *   00e33380  clr.w (0x3e,A2)                          ; pending_mode
 *   00e33384  clr.w (0x46,A2)                          ; flags
 *   00e33388  subq.l #2 / move.w #1 / pea (0x4a,PC) / pea (A2) / jsr kbd_$set_type
 *             ; 0xE33390 + 0x4A = 0xE333DA: bytes 32 00 = "2"; lea (0xc,SP) pops
 *             ; the result slot too (kbd_$set_type is a Pascal function)
 *   00e3339e  lea (0x58,A0),A1 / move.l A1,(0x30,A2)   ; tpad_buffer = &TERM_$TPAD_BUFFER
 *   00e333ac  pea (0x4c,A2) / jsr EC_$INIT             ; args reclaimed by unlk
 *   00e333b6  move.l #0x10001,(0x58,A2)                ; ring_head = ring_tail = 1
 *   00e333be  move.w #0x40,(0x5c,A2)                   ; ring_size
 *   00e333c4  move.l #0x10001,(0x9e,A2)                ; flags2
 *   00e333cc  move.w #0x40,(0xa2,A2)                   ; value2
 */

#include "kbd/kbd_internal.h"

/* 0x00E333DA, `gsk read 0xE333DA 2`: 32 00 - the type string "2" */
static const uint8_t kbd_$c_default_type[2] = { '2', 0x00 };

void KBD_$INIT(kbd_state_t *state)
{
    /* 0x00E33374 */
    TERM_$TPAD_BUFFER.head = 0;
    TERM_$TPAD_BUFFER.tail = 0;

    /* 0x00E33378..0x00E33384 */
    state->state = 0;
    state->sub_state = 0;
    state->kbd_type_idx = 0;
    state->pending_mode = 0;
    state->flags = 0;

    /* 0x00E33388..0x00E3339A: length 1 */
    kbd_$set_type(state, (uint8_t *)kbd_$c_default_type, 1);

    /* 0x00E3339E..0x00E333A8 */
    state->tpad_buffer = &TERM_$TPAD_BUFFER;

    /* 0x00E333AC..0x00E333B0 */
    EC_$INIT(&state->ec);

    /* 0x00E333B6..0x00E333CC */
    state->ring_head = 1;
    state->ring_tail = 1;
    state->ring_size = KBD_RING_SIZE;
    state->flags2 = 0x00010001;
    state->value2 = 0x0040;
}
