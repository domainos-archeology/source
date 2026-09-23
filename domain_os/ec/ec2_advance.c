/*
 * EC2_$ADVANCE - Advance a Level 2 Event Count
 *
 * Increments the eventcount and, if the waiter word is non-zero, hands the
 * eventcount to EC2_$WAKEUP to release the waiters.
 *
 * Parameters:
 *   ec         - EC2 pointer (argument 1, (0x8,A6)); values <= 0x3E8 are
 *                EC2 indices, not addresses, and are rejected here
 *   status_ret - Status return (argument 2, (0xC,A6)), passed by reference
 *
 * Original address: 0x00e42cae (map: EC2U, 0xE42C60 size 0x8C)
 * Re-emitted from the disassembly 0x00E42CAE-0x00E42CE8.
 */

#include "ec/ec_internal.h"

void EC2_$ADVANCE(ec2_$eventcount_t *ec, status_$t *status_ret)
{
    /* 0x00E42CB8-0x00E42CCA: `cmpi.l #0x3e8,D0 / bhi` - an UNSIGNED
     * compare of the raw pointer value against 0x3E8.  Not above means
     * this is an index, not a level-2 eventcount address. */
    if (ARCH_PTR_TO_VA(ec) <= 0x3E8) {
        *status_ret = status_$ec2_bad_event_count;      /* 0x180004 */
    } else {
        /* 0x00E42CCC-0x00E42CD0: clear the status and bump the value. */
        *status_ret = status_$ok;
        ec->value++;

        /* 0x00E42CD2-0x00E42CDC: `tst.w (0x4,A0)` - a waiter is recorded,
         * so EC2_$WAKEUP(ec, status_ret) releases it (status by reference). */
        if (ec->awaiters != 0) {
            EC2_$WAKEUP(ec, status_ret);
        }
    }
    /* 0x00E42CE2: common exit. */
}
