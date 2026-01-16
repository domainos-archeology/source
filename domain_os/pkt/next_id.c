/*
 * PKT_$NEXT_ID - Get next short packet ID
 *
 * Returns a unique short (16-bit) packet ID. IDs cycle from 1 to 64000.
 * Thread-safe via spin lock.
 *
 * The assembly shows:
 * 1. Acquire spin lock at offset 0x50 from base (0xE24CEC)
 * 2. Read current ID from offset 0x5C (0xE24CF8)
 * 3. Increment and wrap at 64000 (0xFA00 = -0x600 unsigned)
 * 4. Release spin lock
 * 5. Return the previous ID
 *
 * Original address: 0x00E1248E
 */

#include "pkt/pkt_internal.h"

int16_t PKT_$NEXT_ID(void)
{
    int16_t result;
    ml_$spin_token_t token;

    /* Acquire spin lock */
    token = ML_$SPIN_LOCK(&PKT_$DATA->spin_lock);

    /* Get current ID and increment */
    result = PKT_$DATA->short_id;
    PKT_$DATA->short_id++;

    /* Wrap at 64000 back to 1 */
    if (PKT_$DATA->short_id > PKT_MAX_SHORT_ID) {
        PKT_$DATA->short_id = 1;
    }

    /* Release spin lock */
    ML_$SPIN_UNLOCK(&PKT_$DATA->spin_lock, token);

    return result;
}
