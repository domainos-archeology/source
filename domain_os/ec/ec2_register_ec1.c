/*
 * EC2_$REGISTER_EC1 - Register an EC1 for EC2 access
 *
 * Parameters:
 *   ec1 - EC1 pointer to register
 *   status_ret - Status return
 *
 * Returns:
 *   EC2 index for the registered EC1
 *
 * Original address: 0x00e4293c
 */

#include "ec_internal.h"
#include "ml/ml.h"

#define MAX_REGISTERED_EC1  0x100
#define status_$ec2_registration_full   0x00180001

void *EC2_$REGISTER_EC1(ec_$eventcount_t *ec1, status_$t *status_ret)
{
    status_$t status = status_$ok;
    int16_t probe, index;
    void *result;

    ML_$LOCK(EC2_LOCK_ID);

    probe = DAT_00e7cf06 - 1;
    if (probe >= 0) {
        index = 1;
        while (probe >= 0) {
            if ((ec_$eventcount_t *)DAT_00e7caf8[index] == ec1) {
                result = (void *)(uintptr_t)index;
                goto done;
            }
            index++;
            probe--;
        }
    }

    if (_DAT_00e7cf04 >= MAX_REGISTERED_EC1) {
        status = status_$ec2_registration_full;
        result = NULL;
    } else {
        _DAT_00e7cf04++;
        DAT_00e7caf8[_DAT_00e7cf04] = ec1;
        result = (void *)(uintptr_t)_DAT_00e7cf04;
    }

done:
    ML_$UNLOCK(EC2_LOCK_ID);
    *status_ret = status;
    return result;
}
