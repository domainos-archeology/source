/*
 * EC2_$READ - Read a Level 2 Event Count value
 *
 * For direct pointers (> 0x3E8), reads directly.
 * For indices (0-0x3E8), calls EC2_$GET_VAL.
 *
 * Parameters:
 *   ec - EC2 pointer or index
 *
 * Returns:
 *   Current value of the eventcount
 *
 * Original address: 0x00e42c7c
 */

#include "ec.h"

int32_t EC2_$READ(ec2_$eventcount_t *ec)
{
    status_$t status;
    ec2_$eventcount_t index_holder;

    if ((uintptr_t)ec > 0x3E8) {
        /* Direct pointer mode */
        return ec->value;
    } else {
        /* Index mode - use GET_VAL */
        index_holder.value = (int32_t)(uintptr_t)ec;
        return EC2_$GET_VAL(&index_holder, &status);
    }
}
