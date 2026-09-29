/*
 * ml_data.c - ML Module Global Data Definitions
 *
 * This file defines the global variables used by the ML (Mutual Exclusion
 * Locks) module, the same on every build (source-702z).
 *
 * Original M68K addresses:
 *   LOCK_BYTES:       0xE20BC4 (32 bytes, one per lock)
 *   LOCK_EVENTS:      0xE20BE4 (16 bytes per lock, 32 locks = 512 bytes)
 *
 * Memory Layout:
 *   0xE20BC4-0xE20BE3: Lock bytes array (32 bytes)
 *   0xE20BE4-0xE20DE3: Lock event structures (32 x 16 bytes = 512 bytes)
 *
 * Each lock event structure (16 bytes) contains:
 *   Offset 0x00: Event count value (4 bytes)
 *   Offset 0x04: Forward link pointer (4 bytes, self-referential when empty)
 *   Offset 0x08: Backward link pointer (4 bytes, self-referential when empty)
 *   Offset 0x0C: Wait count (4 bytes)
 */

#include "base/base.h"
#include "ec/ec.h"
#include "ml/ml_internal.h"

/*
 * ============================================================================
 * Lock State
 * ============================================================================
 */

/*
 * Lock bytes array (map LOCK_BYTE)
 *
 * Each byte represents the state of one lock:
 *   Bit 0: Lock is held
 *   Bits 1-7: Reserved
 *
 * The kernel supports up to 32 locks (0-31).  Zero in the image.
 *
 * Original address: 0xE20BC4
 */
volatile uint8_t ML_$LOCK_BYTES[ML_NUM_LOCKS];

/*
 * Lock event structures (map LOCK_$EVENT_LISTS)
 *
 * Each lock has an associated event count for blocking waits.  The image
 * ships every one with value 0, its waiter list empty (head and tail = the
 * eventcount's own address, the state EC_$INIT produces) and wait_count 0,
 * e.g. 0xE20BE4: 00000000 00e20be4 00e20be4 00000000.
 *
 * Original address: 0xE20BE4
 */
#define ML_EVENT_SELF(i)                                                     \
    { .ec = { .value = 0,                                                    \
              .waiter_list_head =                                            \
                  (ec_$eventcount_waiter_t *)&ML_$LOCK_EVENTS[i].ec,         \
              .waiter_list_tail =                                            \
                  (ec_$eventcount_waiter_t *)&ML_$LOCK_EVENTS[i].ec },       \
      .wait_count = 0 }
#define ML_EVENT_SELF_8(b)                                                   \
    ML_EVENT_SELF((b) + 0), ML_EVENT_SELF((b) + 1), ML_EVENT_SELF((b) + 2), \
    ML_EVENT_SELF((b) + 3), ML_EVENT_SELF((b) + 4), ML_EVENT_SELF((b) + 5), \
    ML_EVENT_SELF((b) + 6), ML_EVENT_SELF((b) + 7)
_Static_assert(ML_NUM_LOCKS == 32, "ML_EVENT_SELF_8 spells out 32 eventcounts");

ml_$lock_event_t ML_$LOCK_EVENTS[ML_NUM_LOCKS] = {
    ML_EVENT_SELF_8(0), ML_EVENT_SELF_8(8), ML_EVENT_SELF_8(16), ML_EVENT_SELF_8(24),
};
