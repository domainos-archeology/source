/*
 * ml_data.c - ML Module Global Data Definitions
 *
 * This file defines the global variables used by the ML (Mutual Exclusion
 * Locks) module for non-M68K architectures.
 *
 * Original M68K addresses:
 *   LOCK_BYTES:       0xE20BC4 (32 bytes, one per lock)
 *   LOCK_EVENTS:      0xE20BE4 (16 bytes per lock, 32 locks = 512 bytes)
 *
 * Memory Layout:
 *   0xE20BC4-0xE20BE3: Lock bytes array (32 bytes)
 *   0xE20BE4-0xE22BE3: Lock event structures (32 x 16 bytes = 512 bytes)
 *
 * Each lock event structure (16 bytes) contains:
 *   Offset 0x00: Event count value (4 bytes)
 *   Offset 0x04: Forward link pointer (4 bytes, self-referential when empty)
 *   Offset 0x08: Backward link pointer (4 bytes, self-referential when empty)
 *   Offset 0x0C: Wait count (4 bytes)
 */

#include "base/base.h"
#include "ec/ec.h"
#include "ml/ml.h"

#if !defined(M68K)

/*
 * ============================================================================
 * Lock State
 * ============================================================================
 */

/*
 * Lock bytes array
 *
 * Each byte represents the state of one lock:
 *   Bit 0: Lock is held
 *   Bits 1-7: Reserved
 *
 * The kernel supports up to 32 locks (0-31).
 *
 * Original address: 0xE20BC4
 */
volatile uint8_t ML_$LOCK_BYTES[32] = { 0 };

/*
 * Lock event structures
 *
 * Each lock has an associated event count structure for blocking waits.
 * The structure is initialized with self-referential pointers (empty list).
 *
 * Original address: 0xE20BE4
 */
ec_$eventcount_t ML_$LOCK_EVENTS[32] = { { 0 } };

#endif /* !M68K */
