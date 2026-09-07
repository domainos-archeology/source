/*
 * suma/suma_internal.h - SUMA Subsystem Internal Definitions
 *
 * Internal header for SUMA subsystem implementation.
 */

#ifndef SUMA_INTERNAL_H
#define SUMA_INTERNAL_H

#include "suma/suma.h"
#include "dxm/dxm.h"
#include "term/term.h"

/* TERM_$ENQUEUE_TPAD (0x00e72472) is declared in term/term.h */

/*
 * Cell holding TERM_$ENQUEUE_TPAD's address
 *
 * SUMA_$RCV pushes the ADDRESS of this cell to DXM_$ADD_CALLBACK
 * (0x00E1AE84); dxm_$callback_t keeps the queue entry 16 bytes on every
 * target (source-wy9y).
 *
 * Original address: 0x00e1aecc (relative to SUMA_$RCV at PC+0x3a)
 *
 * The cell holds 0x00e72472 (TERM_$ENQUEUE_TPAD); defined in
 * suma/suma_data.c.  KBD's cell at 0x00e1ce90 is a separate literal with
 * the same contents (term/term_data.c).
 */
extern dxm_$callback_t PTR_TERM_$ENQUEUE_TPAD_00e1aecc;

#endif /* SUMA_INTERNAL_H */
