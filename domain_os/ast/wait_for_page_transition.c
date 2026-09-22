/*
 * ast_$wait_for_page_transition - Sleep until the PMAP in-transition
 *                                 eventcount advances
 *
 * Called with the PMAP lock held: takes the eventcount's current value
 * +1, releases the lock, waits on that one eventcount with EC_$WAITN, and
 * retakes the lock.  The caller then re-tests its segment map entry.
 *
 * Original address: 0x00E00C08 (76 bytes).  A5 is inherited (every
 * caller is AST code with A5 = 0xE1DC80): (0x44C,A5) is
 * AST_$PMAP_IN_TRANS_EC.  Frame: (-0x8) the value to wait for, (-0xC)
 * the eventcount pointer whose ADDRESS is the list argument.
 */

#include "ast/ast_internal.h"

void ast_$wait_for_page_transition(void)
{
    ec_$eventcount_t *ec_ptr;   /* (-0xC,A6) */
    int32_t wait_value;         /* (-0x8,A6) */

    /* 0x00E00C0C..0x00E00C12 */
    wait_value = AST_$PMAP_IN_TRANS_EC.value + 1;

    /* 0x00E00C16..0x00E00C22 */
    ML_$UNLOCK(PMAP_LOCK_ID);

    /* 0x00E00C24..0x00E00C40: EC_$WAITN(&ec_ptr, &wait_value, 1) */
    ec_ptr = &AST_$PMAP_IN_TRANS_EC;
    (void)EC_$WAITN(&ec_ptr, &wait_value, 1);

    /* 0x00E00C44..0x00E00C4A */
    ML_$LOCK(PMAP_LOCK_ID);
}
