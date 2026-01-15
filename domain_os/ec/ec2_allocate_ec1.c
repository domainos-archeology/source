/*
 * EC2_$ALLOCATE_EC1 - Allocate a new EC1 from PBU pool
 *
 * Returns:
 *   EC2 index (0x101-0x120) for allocated EC1
 *
 * Original address: 0x00e429da
 */

#include "ec_internal.h"
#include "ml/ml.h"

void *EC2_$ALLOCATE_EC1(status_$t *status_ret)
{
    uint32_t index;
    int16_t probe;
    ec_$eventcount_t *ec;
    void *result = NULL;

    ML_$LOCK(EC2_LOCK_ID);

    probe = 0x1F;
    index = 0;
    ec = (ec_$eventcount_t *)EC2_PBU_ECS_BASE;

    while (probe >= 0) {
        uint32_t mask = 1 << (index & 0x1F);

        /* Check pending release bitmap first */
        if ((DAT_00e7cefc & mask) != 0) {
            /* Check if reference count is zero */
            uint8_t *ec_bytes = (uint8_t *)ec;
            if (*(int16_t *)(ec_bytes + 0x0E) == 0) {
                /* Reuse this entry */
                DAT_00e7cf00 |= mask;
                DAT_00e7cefc &= ~mask;
                goto found;
            }
        }

        /* Check allocation bitmap */
        if ((DAT_00e7cf00 & mask) == 0) {
            /* Free entry found */
            DAT_00e7cf00 |= mask;
            goto found;
        }

        index++;
        ec = (ec_$eventcount_t *)((uintptr_t)ec + 0x18);
        probe--;
    }

    /* No free entry */
    ML_$UNLOCK(EC2_LOCK_ID);
    *status_ret = status_$ec2_unable_to_allocate_level_1_eventcount;
    return NULL;

found:
    result = (void *)(uintptr_t)(index + 0x101);
    EC_$INIT(ec);
    /* Clear reference count at offset 0x0E */
    *(int16_t *)((uint8_t *)ec + 0x0E) = 0;

    ML_$UNLOCK(EC2_LOCK_ID);
    *status_ret = status_$ok;
    return result;
}
