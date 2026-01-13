/*
 * EC2_$GET_EC1_ADDR - Get address of EC1 from EC2 index
 *
 * Parameters:
 *   ec - EC2 containing index
 *   status_ret - Status return
 *
 * Returns:
 *   Pointer to EC1 structure
 *
 * Original address: 0x00e42a8a
 */

#include "ec.h"

extern uint32_t _DAT_00e7cf04;
extern void *DAT_00e7caf8[];

ec_$eventcount_t *EC2_$GET_EC1_ADDR(ec2_$eventcount_t *ec, status_$t *status_ret)
{
    uint32_t index;
    ec_$eventcount_t *result = NULL;
    status_$t status;

    ML_$LOCK(EC2_LOCK_ID);

    index = (uint32_t)ec->value;

    if (index >= 2 && index <= _DAT_00e7cf04) {
        /* Registered EC1 */
        result = (ec_$eventcount_t *)DAT_00e7caf8[index];
        status = status_$ok;
    } else if (index >= 0x101 && index <= 0x120) {
        /* PBU pool EC1 */
        uint16_t pbu_index = (uint16_t)(index - 0x101);
        if ((DAT_00e7cf00 & (1 << (pbu_index & 0x1F))) == 0) {
            status = status_$ec2_level_1_ec_not_allocated;
        } else {
            result = (ec_$eventcount_t *)(EC2_PBU_ECS_BASE + pbu_index * 0x18);
            status = status_$ok;
        }
    } else {
        status = status_$ec2_bad_event_count;
    }

    ML_$UNLOCK(EC2_LOCK_ID);
    *status_ret = status;
    return result;
}
