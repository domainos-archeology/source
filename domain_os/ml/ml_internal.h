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
 * Internal scheduling helper
 *
 * Called after lock release to handle rescheduling.
 *
 * Original address: 0x00e20824
 */
void FUN_00e20824(void);

/*
 * Error status codes (defined in misc/crash_system.c)
 */
extern status_$t Lock_ordering_violation;
extern status_$t Illegal_lock_err;

#endif /* ML_INTERNAL_H */
