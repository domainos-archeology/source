/*
 * ML Internal - Mutual Exclusion Locks Internal Definitions
 *
 * This header contains internal definitions used within the ML subsystem.
 * External code should use ml/ml.h instead.
 */

#ifndef ML_INTERNAL_H
#define ML_INTERNAL_H

#include "ml/ml.h"
#include "proc1/proc1.h"
#include "ec/ec.h"
#include "misc/misc.h"

/*
 * proc1_$add_ready_body - Priority-ordered ready list insertion
 *
 * Re-inserts a PCB into the ready list in priority order after
 * lock release or exclusion stop. Uses register calling convention
 * (A1 = PCB pointer).
 *
 * Original address: 0x00e20824
 */
void proc1_$add_ready_body(void);

/*
 * Error status codes (defined in misc/crash_system.c)
 */
extern status_$t Lock_ordering_violation;
extern status_$t Illegal_lock_err;

#endif /* ML_INTERNAL_H */
