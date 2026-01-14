/*
 * ML_$EXCLUSION_CHECK - Check if exclusion region is locked
 *
 * Tests whether an exclusion lock is currently held.
 *
 * Returns non-zero (true) if the state is >= 0, meaning the region
 * is occupied. Returns 0 (false) if state is -1 (unlocked).
 *
 * Original address: 0x00E20E4A
 */

#include "ml.h"

int8_t ML_$EXCLUSION_CHECK(ml_$exclusion_t *excl)
{
    /*
     * State semantics:
     *   -1: Unlocked (return 0/false)
     *   0+: Locked (return non-zero/true)
     *
     * The original uses SGE (set if greater or equal) after TST.W,
     * which sets the byte to 0xFF if >= 0, or 0x00 if < 0.
     * We return the negation: true if locked.
     */
    return (excl->f5 >= 0) ? -1 : 0;
}
