/*
 * EC2_$RELEASE_EC1 - Release an allocated EC1
 *
 * Parameters:
 *   ec - EC2 index of EC1 to release
 *   status_ret - Status return
 *
 * Original address: 0x00e42b32
 */

#include "ec_internal.h"
#include "ml/ml.h"

void EC2_$RELEASE_EC1(ec2_$eventcount_t *ec, status_$t *status_ret)
{
    uint32_t index;
    uint16_t pbu_index;
    status_$t status;

    ML_$LOCK(EC2_LOCK_ID);

    index = (uint32_t)ec->value;

    if (index < 0x101 || index > 0x120) {
        status = status_$ec2_bad_event_count;
    } else {
        pbu_index = (uint16_t)(index - 0x101);
        uint32_t mask = 1 << (pbu_index & 0x1F);

        if ((DAT_00e7cf00 & mask) == 0) {
            status = status_$ec2_level_1_ec_not_allocated;
        } else {
            ec_$eventcount_t *pbu_ec = (ec_$eventcount_t *)
                (EC2_PBU_ECS_BASE + pbu_index * 0x18);
            int16_t *refcount = (int16_t *)((uint8_t *)pbu_ec + 0x0E);

            status = status_$ok;

            if (*refcount == 0) {
                /* No references - free immediately */
                DAT_00e7cf00 &= ~mask;
            } else {
                /* Has references - wake all and mark pending */
                EC_$ADVANCE_ALL(pbu_ec);
                DAT_00e7cefc |= mask;
            }
        }
    }

    ML_$UNLOCK(EC2_LOCK_ID);
    *status_ret = status;
}
