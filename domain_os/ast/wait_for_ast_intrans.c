/*
 * AST_$WAIT_FOR_AST_INTRANS - Wait for AST in-transition to complete
 *
 * Waits for the AST in-transition event counter to advance,
 * indicating that an in-flight AST operation has completed.
 * Used for synchronization when an AOTE/ASTE is being modified.
 *
 * Original address: 0x00e00c54
 */

#include "ast.h"

void AST_$WAIT_FOR_AST_INTRANS(void)
{
    ec_$eventcount_t *ec_ptr;
    int32_t wait_value;

    /* Get current value + 1 to wait for next advance */
    wait_value = AST_$AST_IN_TRANS_EC.value + 1;

    /* Release lock while waiting */
    ML_$UNLOCK(AST_LOCK_ID);

    /* Wait for event counter to reach target value */
    ec_ptr = &AST_$AST_IN_TRANS_EC;
    EC_$WAITN(&ec_ptr, &wait_value, 1);

    /* Reacquire lock */
    ML_$LOCK(AST_LOCK_ID);
}
