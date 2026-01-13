/*
 * EC2_$INIT - Initialize a Level 2 Event Count
 *
 * Only initializes if the pointer is > 0x3E8 (direct pointer mode).
 * For index mode (0-0x3E8), the EC2 was already initialized by
 * EC2_$REGISTER_EC1 or EC2_$ALLOCATE_EC1.
 *
 * Parameters:
 *   ec - EC2 pointer (must be > 0x3E8 for initialization)
 *
 * Original address: 0x00e42c60
 */

#include "ec.h"

void EC2_$INIT(ec2_$eventcount_t *ec)
{
    /* Only initialize if this is a direct pointer (> 0x3E8) */
    if ((uintptr_t)ec > 0x3E8) {
        ec->value = 0;
        ec->awaiters = 0;
    }
}
