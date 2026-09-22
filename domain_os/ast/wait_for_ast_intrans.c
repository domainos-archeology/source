/*
 * AST_$WAIT_FOR_AST_INTRANS - Sleep until the AST in-transition
 *                             eventcount advances
 *
 * Called with the AST lock held: takes the eventcount's current value +1,
 * releases the lock, waits on that one eventcount with EC_$WAITN, and
 * retakes the lock.  The caller then re-examines whatever it was waiting
 * on.
 *
 * Original address: 0x00E00C54 (88 bytes).  `pea (A5)` / `lea (0xe1dc80).l,A5`:
 * (0x428,A5) is AST_$AST_IN_TRANS_EC.  Frame: (-0x4) the value to wait
 * for, (-0x8) the eventcount pointer whose ADDRESS is the list argument.
 * The `subq.l #0x2,SP` before EC_$WAITN's pushes is its discarded word
 * result.
 */

#include "ast/ast_internal.h"

void AST_$WAIT_FOR_AST_INTRANS(void)
{
    ec_$eventcount_t *ec_ptr;   /* (-0x8,A6) */
    int32_t wait_value;         /* (-0x4,A6) */

    /* 0x00E00C60..0x00E00C66 */
    wait_value = AST_$AST_IN_TRANS_EC.value + 1;

    /* 0x00E00C6A..0x00E00C76 */
    ML_$UNLOCK(AST_LOCK_ID);

    /* 0x00E00C78..0x00E00C94: EC_$WAITN(&ec_ptr, &wait_value, 1) */
    ec_ptr = &AST_$AST_IN_TRANS_EC;
    (void)EC_$WAITN(&ec_ptr, &wait_value, 1);

    /* 0x00E00C98..0x00E00C9E */
    ML_$LOCK(AST_LOCK_ID);
}
