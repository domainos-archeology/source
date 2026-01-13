/*
 * EC2_$GET_VAL - Get value from an EC2
 *
 * Translates an EC2 index to its underlying EC1 and returns the value.
 *
 * Index ranges:
 *   0: Invalid
 *   1-DAT_00e7cf04: Registered EC1s (in registration table)
 *   0x101-0x120: Allocated EC1s (in PBU pool)
 *
 * Parameters:
 *   ec - EC2 containing the index
 *   status_ret - Status return
 *
 * Returns:
 *   Current value (0x7FFFFFFF on error)
 *
 * Original address: 0x00e42be0
 */

#include "ec.h"

/* External data */
extern void *DAT_00e7caf8[];    /* Registration table (EC1 pointers indexed by id) */
extern uint32_t _DAT_00e7cf04;  /* Max registered EC1 index */

int32_t EC2_$GET_VAL(ec2_$eventcount_t *ec, status_$t *status_ret)
{
    uint32_t index;
    uint16_t pbu_index;
    ec_$eventcount_t *ec1;

    *status_ret = status_$ok;

    index = (uint32_t)ec->value;

    /* Check for registered EC1 */
    if (index != 0 && index <= _DAT_00e7cf04) {
        ec1 = (ec_$eventcount_t *)DAT_00e7caf8[index];
        return ec1->value;
    }

    /* Check for allocated EC1 (PBU pool: 0x101-0x120) */
    if (index >= 0x101 && index <= 0x120) {
        pbu_index = (uint16_t)(index - 0x101);

        /* Check allocation bitmap */
        if ((DAT_00e7cf00 & (1 << (pbu_index & 0x1F))) != 0) {
            /* Get value from PBU EC pool */
            ec_$eventcount_t *pbu_ec = (ec_$eventcount_t *)
                ((uintptr_t)&EC2_$PBU_ECS + (pbu_index * 0x18));
            return pbu_ec->value;
        }

        *status_ret = status_$ec2_level_1_ec_not_allocated;
        return 0x7FFFFFFF;
    }

    *status_ret = status_$ec2_bad_event_count;
    return 0x7FFFFFFF;
}
