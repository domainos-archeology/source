/*
 * ML_$COND_EXCLUSION_START - Conditionally enter an exclusion region
 *
 * Attempts to enter an exclusion region without blocking. If the region
 * is already occupied, returns immediately with a failure indication.
 *
 * Original address: 0x00E20E56
 */

#include "ml.h"

int8_t ML_$COND_EXCLUSION_START(ml_$exclusion_t *excl)
{
    ml_$spin_token_t token;

    /* Disable interrupts for atomic check-and-set */
    token = ML_$SPIN_LOCK(NULL);

    if (excl->f5 >= 0) {
        /* Region is occupied - return failure */
        ML_$SPIN_UNLOCK(NULL, token);
        return -1;  /* Returns non-zero to indicate "already locked" */
    }

    /* Region is free - claim it */
    excl->f5 = 0;

    ML_$SPIN_UNLOCK(NULL, token);

    return 0;  /* Returns 0 to indicate success */
}
