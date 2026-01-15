/*
 * ast_$wait_for_page_transition - Wait for page transition to complete
 *
 * Waits for the PMAP in-transition event counter to advance,
 * indicating that an in-flight page operation has completed.
 * Used for synchronization when pages are being mapped/unmapped.
 *
 * Original address: 0x00e00c08
 */

#include "ast/ast_internal.h"

void ast_$wait_for_page_transition(void)
{
    ec_$eventcount_t *ec_ptr;
    int32_t wait_value;

    /* Get current value + 1 to wait for next advance */
    wait_value = AST_$PMAP_IN_TRANS_EC.value + 1;

    /* Release PMAP lock while waiting */
    ML_$UNLOCK(PMAP_LOCK_ID);

    /* Wait for event counter to reach target value */
    ec_ptr = &AST_$PMAP_IN_TRANS_EC;
    EC_$WAITN(&ec_ptr, &wait_value, 1);

    /* Reacquire PMAP lock */
    ML_$LOCK(PMAP_LOCK_ID);
}
