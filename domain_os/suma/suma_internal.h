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

/*
 * Callback function for enqueueing tablet pad events
 *
 * Original address: 0x00e72472
 */
extern void TERM_$ENQUEUE_TPAD(void **param1);

/*
 * Pointer to TERM_$ENQUEUE_TPAD callback
 *
 * Original address: 0x00e1aecc (relative to SUMA_$RCV at PC+0x3a)
 */
extern void (*PTR_TERM_$ENQUEUE_TPAD_00e1aecc)(void **);

#endif /* SUMA_INTERNAL_H */
