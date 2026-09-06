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
 * Pointer to TERM_$ENQUEUE_TPAD callback
 *
 * Original address: 0x00e1aecc (relative to SUMA_$RCV at PC+0x3a)
 */
extern void (*PTR_TERM_$ENQUEUE_TPAD_00e1aecc)(void **);

#endif /* SUMA_INTERNAL_H */
