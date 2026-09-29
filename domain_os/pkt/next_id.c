/*
 * PKT_$NEXT_ID - Get next short packet ID
 *
 * Returns a unique short (16-bit) packet ID. IDs cycle from 1 to 64000.
 * Thread-safe via spin lock.
 *
 * The assembly shows:
 * 1. Acquire spin lock at offset 0x50 from base (0xE24CEC)
 * 2. Read current ID from offset 0x5C (0xE24CF8)
 * 3. Increment and wrap at 64000 (0xFA00, the "cmpi.w #-0x600" operand; the
 *    following `bls` makes the comparison unsigned)
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
    token = ML_$SPIN_LOCK(&PKT_$DATA.spin_lock);

    /*
     * 0x00E124AC-0x00E124B0: take the current value, then bump the cell.
     */
    result = (int16_t)PKT_$DATA.short_id;
    PKT_$DATA.short_id++;

    /*
     * 0x00E124B4-0x00E124C0: "cmpi.w #-0x600,(0x5c,A5) / bls" - an UNSIGNED
     * compare against 0xFA00 = 64000, so the counter wraps to 1 once it has
     * passed 64000 rather than running on through the negative words a signed
     * compare would allow.  short_id is uint16_t for exactly this reason.
     */
    if (PKT_$DATA.short_id > PKT_MAX_SHORT_ID) {
        PKT_$DATA.short_id = 1;
    }

    /* Release spin lock */
    ML_$SPIN_UNLOCK(&PKT_$DATA.spin_lock, token);

    return result;
}
