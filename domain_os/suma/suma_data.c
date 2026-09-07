/*
 * suma/suma_data.c - SUMA (Summagraphics tablet) Subsystem Global Data
 *
 * Original addresses:
 *   SUMA_$STATE:                     0x00e2dd88
 *   PTR_TERM_$ENQUEUE_TPAD_00e1aecc: 0x00e1aecc
 */

#include "suma/suma_internal.h"

/*
 * SUMA_$STATE - tablet receive state
 *
 * 0x2e bytes at 0x00e2dd88.  The image holds all zeroes there
 * (gsk read 0x00e2dd88 0x30); SUMA_$INIT (0x00e33224) fills in
 * tpad_buffer, rcv_state, cur_id_flags and threshold at run time.
 */
suma_state_t SUMA_$STATE;

/*
 * PTR_TERM_$ENQUEUE_TPAD_00e1aecc - cell holding TERM_$ENQUEUE_TPAD's address
 *
 * SUMA_$RCV (0x00e1ad18) queues the tablet-pad drain callback with
 *
 *   00e1ae90    pea (0x3a,PC)          ; -> 0x00e1ae90 + 2 + 0x3a = 0x00e1aecc
 *   00e1ae94    move.l #0xe2adc4,-(SP) ; DXM_$UNWIRED_Q
 *   00e1ae9a    jsr 0x00e16fe0.l       ; DXM_$ADD_CALLBACK
 *
 * so DXM receives the ADDRESS of this cell.  The image has
 *
 *   00e1aecc  00 e7 24 72            ; = TERM_$ENQUEUE_TPAD (0x00e72472)
 *
 * This is a distinct cell from KBD's PTR_TERM_$ENQUEUE_TPAD_00e1ce90
 * (0x00e1ce90, defined in term/term_data.c): each module carries its own
 * literal in its own code region, and both happen to hold 0x00e72472.
 */
DXM_$DEFINE_CALLBACK_CELL(PTR_TERM_$ENQUEUE_TPAD_00e1aecc, TERM_$ENQUEUE_TPAD);
