/*
 * pmap_$wait_in_transit - Wait for PMAP in-transit event count to advance
 *
 * Waits for the AST_$PMAP_IN_TRANS_EC event count to advance past its
 * current value. Used when a page is in transit (being written to disk)
 * and we need to wait for the I/O to complete before proceeding.
 *
 * The function:
 * 1. Reads the current AST_$PMAP_IN_TRANS_EC value + 1
 * 2. Unlocks PMAP lock (lock 0x14) so the I/O can complete
 * 3. Waits (via EC_$WAITN) for the event count to reach the target value
 * 4. Re-acquires the PMAP lock
 *
 * This is an internal helper called from PMAP_$FLUSH when a page write
 * did not perform any new writes but invalid pages were found.
 *
 * Original address: 0x00e12d38
 * Size: 70 bytes
 */

#include "pmap/pmap_internal.h"
#include "ast/ast.h"

/*
 * Pointer to AST_$PMAP_IN_TRANS_EC, used as the EC array for EC_$WAITN.
 * In the original code this is stored as PC-relative data at 0xe12d80.
 */
static ec_$eventcount_t *pmap_in_trans_ec_ptr = &AST_$PMAP_IN_TRANS_EC;

void pmap_$wait_in_transit(void)
{
    int32_t wait_val[2];

    /* Wait for the event count to advance past current value */
    wait_val[0] = AST_$PMAP_IN_TRANS_EC.value + 1;

    ML_$UNLOCK(PMAP_LOCK_ID);

    EC_$WAITN(&pmap_in_trans_ec_ptr, wait_val, 1);

    ML_$LOCK(PMAP_LOCK_ID);
}
