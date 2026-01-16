/*
 * PKT_$NEXT_LONG_ID - Get next long packet ID
 *
 * Returns a unique long (32-bit) packet ID. IDs increment without wrap.
 * Thread-safe via spin lock.
 *
 * The assembly shows:
 * 1. Acquire spin lock at offset 0x50 from base (0xE24CEC)
 * 2. Read current ID from offset 0x60 (0xE24CFC)
 * 3. Increment (no wrap check)
 * 4. Release spin lock
 * 5. Return the previous ID
 *
 * Original address: 0x00E124DC
 */

#include "pkt/pkt_internal.h"

int32_t PKT_$NEXT_LONG_ID(void)
{
    int32_t result;
    ml_$spin_token_t token;

    /* Acquire spin lock */
    token = ML_$SPIN_LOCK(&PKT_$DATA->spin_lock);

    /* Get current ID and increment */
    result = PKT_$DATA->long_id;
    PKT_$DATA->long_id++;

    /* Release spin lock */
    ML_$SPIN_UNLOCK(&PKT_$DATA->spin_lock, token);

    return result;
}
