/*
 * UID_$GEN - Generate a new unique identifier
 *
 * Generates a globally unique 64-bit identifier. Uses the system clock
 * for the high word and a node ID + counter for the low word.
 * Thread-safe via spin lock.
 *
 * The algorithm:
 * 1. Get current clock value minus 0xF0 (offset for uniqueness window)
 * 2. Acquire spin lock
 * 3. If clock > stored value, use clock as new high word
 * 4. Otherwise, wait until clock advances to avoid duplicates
 * 5. Copy current state to output
 * 6. Increment counter in low byte (bits 4-7 of low word byte 0)
 * 7. If counter overflows, increment high word
 * 8. Release spin lock
 *
 * Parameters:
 *   uid_ret - Pointer to receive the generated UID
 *
 * Original address: 0x00e1a018
 */

#include "uid.h"
#include "time/time.h"
#include "ml/ml.h"

/*
 * External references:
 *   TIME_$CLOCKH - High word of system clock (0x00e2b0d4)
 *   TIME_$ABS_CLOCK - Get absolute clock value
 *   ML_$SPIN_LOCK - Acquire spin lock
 *   ML_$SPIN_UNLOCK - Release spin lock
 */
extern uint32_t TIME_$CLOCKH;
extern void TIME_$ABS_CLOCK(uint32_t *clock_out);
extern uint16_t ML_$SPIN_LOCK(uint16_t *lock);
extern void ML_$SPIN_UNLOCK(uint16_t *lock, uint16_t token);

void UID_$GEN(uid_t *uid_ret)
{
    uint32_t clock_val;
    uint16_t token;
    uint32_t abs_clock[2];
    uint32_t local_high;
    uint32_t local_low;
    uint8_t counter_nibble;

    /* Get current clock minus offset */
    clock_val = TIME_$CLOCKH - 0xF0;

    /* Acquire spin lock */
    token = ML_$SPIN_LOCK(&UID_$GENERATOR_LOCK);

    /* Check if clock has advanced past stored value */
    if (clock_val > UID_$GENERATOR_STATE.high) {
        /* Clock advanced - use it as new timestamp */
        UID_$GENERATOR_STATE.high = clock_val;
    } else {
        /*
         * Clock hasn't advanced enough - we need to wait to avoid
         * generating duplicate UIDs. The counter nibble (bits 4-7
         * of byte 0 of the low word) is used to generate multiple
         * UIDs per clock tick.
         */
        do {
            TIME_$ABS_CLOCK(abs_clock);

            /* Check if we're still in the same time window */
            if (UID_$GENERATOR_STATE.high != abs_clock[0]) {
                break;  /* High word changed, we can proceed */
            }

            /*
             * Check counter nibble (bits 4-7 of low word's byte 0)
             * vs clock low bits shifted.
             * The counter occupies the upper nibble of byte 0 (big-endian).
             */
            if (((UID_$GENERATOR_STATE.low >> 24) & 0xF0) >> 4 !=
                (abs_clock[1] >> 12) & 0xF) {
                break;  /* Counter space available */
            }

            /* Need to wait - release lock, spin, reacquire */
            ML_$SPIN_UNLOCK(&UID_$GENERATOR_LOCK, token);

            do {
                TIME_$ABS_CLOCK(abs_clock);
            } while (((UID_$GENERATOR_STATE.low >> 24) & 0xF0) >> 4 ==
                     (abs_clock[1] >> 12) & 0xF);

            token = ML_$SPIN_LOCK(&UID_$GENERATOR_LOCK);
        } while (1);
    }

    /* Copy current state to local variables */
    local_high = UID_$GENERATOR_STATE.high;
    local_low = UID_$GENERATOR_STATE.low;

    /*
     * Increment counter in the upper nibble of byte 0 (big-endian).
     * This is bits 28-31 of the low word on big-endian.
     * Add 0x10 to byte 0 (which is the high byte on big-endian).
     */
    counter_nibble = (UID_$GENERATOR_STATE.low >> 24) + 0x10;
    UID_$GENERATOR_STATE.low = (UID_$GENERATOR_STATE.low & 0x00FFFFFF) |
                               ((uint32_t)counter_nibble << 24);

    /* If counter nibble overflowed (upper nibble became 0), increment high */
    if ((counter_nibble & 0xF0) == 0) {
        UID_$GENERATOR_STATE.high++;
    }

    /* Release spin lock */
    ML_$SPIN_UNLOCK(&UID_$GENERATOR_LOCK, token);

    /* Return the UID we generated */
    uid_ret->high = local_high;
    uid_ret->low = local_low;
}
