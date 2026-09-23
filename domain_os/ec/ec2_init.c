/*
 * EC2_$INIT - Initialize a Level 2 Event Count
 *
 * Zeroes the value longword and the waiter word, but only when the
 * argument is above 0x3E8 - a smaller value is an EC2 index (a registered
 * or PBU-pool level-1 eventcount) rather than an address, and is left
 * alone without any status.
 *
 * Parameters:
 *   ec - EC2 pointer (argument 1, (0x8,A6))
 *
 * Original address: 0x00e42c60 (map: EC2U, 0xE42C60 size 0x8C)
 * Re-emitted from the disassembly 0x00E42C60-0x00E42C7A.
 */

#include "ec/ec_internal.h"

void EC2_$INIT(ec2_$eventcount_t *ec)
{
    /* 0x00E42C64-0x00E42C6E: `cmpi.l #0x3e8,D0 / bls` - UNSIGNED compare
     * of the raw pointer value. */
    if (ARCH_PTR_TO_VA(ec) > 0x3E8) {
        /* 0x00E42C70-0x00E42C74: `clr.l (A0) / clr.w (0x4,A0)` */
        ec->value    = 0;
        ec->awaiters = 0;
    }
    /* 0x00E42C78 */
}
