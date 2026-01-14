/*
 * ML_$COND_EXCLUSION_STOP - Leave a conditionally entered exclusion region
 *
 * Leaves an exclusion region that was entered via ML_$COND_EXCLUSION_START.
 * This is a simpler version that just decrements the state counter without
 * waking waiters (since conditional entry doesn't block waiters).
 *
 * Original address: 0x00E20E74
 */

#include "ml.h"

void ML_$COND_EXCLUSION_STOP(ml_$exclusion_t *excl)
{
    /* Simply decrement the state - no need for interrupt protection
     * or waiter wakeup since conditional start doesn't allow waiters */
    excl->f5--;
}
