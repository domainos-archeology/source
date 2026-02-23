/*
 * DIR_$UPDATE_HINT - Update hint after redirect
 *
 * Updates the hint table for a UID after a directory operation
 * redirect or completion. For root directory UIDs (NAME_$ROOT_UID),
 * uses the redirect info instead of the original hint. For non-root
 * directories, passes the original hint through.
 *
 * The hint data consists of two 32-bit values passed to HINT_$ADDI:
 *   [0] = node/location identifier
 *   [1] = extra data (low 20 bits of UID for root, original for others)
 *
 * Special case: for root UIDs, if the redirect port equals ROUTE_$PORT
 * or is zero, no update is performed.
 *
 * Parameters:
 *   uid      - Pointer to directory UID (8 bytes)
 *   hint1    - Original hint value 1 (node)
 *   hint2    - Original hint value 2 (extra)
 *   redirect - Redirect info (contains new node/port)
 *   param5   - Redirect port value
 *
 * Original address: 0x00E4BC76
 * Original size: 106 bytes
 */

#include "dir/dir_internal.h"
#include "name/name.h"

void DIR_$UPDATE_HINT(uid_t *uid, uint32_t hint1, uint32_t hint2,
                      uid_t *redirect, uint32_t param5)
{
    uint32_t hint_data[2];

    /* Check if this is the root directory */
    if (uid->high == NAME_$ROOT_UID.high && uid->low == NAME_$ROOT_UID.low) {
        /* Root directory - use redirect info */

        /* Don't update if redirect port is ROUTE_$PORT or 0 */
        if (param5 == ROUTE_$PORT) {
            return;
        }
        if (param5 == 0) {
            return;
        }

        hint_data[0] = param5;
        hint_data[1] = redirect->low & 0xFFFFF;
    } else {
        /* Non-root directory - use original hint */
        hint_data[0] = hint1;
        hint_data[1] = hint2;
    }

    HINT_$ADDI(redirect, hint_data);
}
