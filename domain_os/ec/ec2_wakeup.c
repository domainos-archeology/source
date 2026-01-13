/*
 * EC2_$WAKEUP - Wake all waiters on an EC2
 *
 * Iterates through the waiter list for the EC2 and advances
 * the underlying EC1s to wake all eligible waiters.
 *
 * Parameters:
 *   ec - EC2 pointer
 *   status_ret - Status return
 *
 * Original address: 0x00e4285a
 */

#include "ec.h"

void EC2_$WAKEUP(ec2_$eventcount_t *ec, status_$t *status_ret)
{
    int32_t ec_value;
    uint16_t waiter_idx;
    uint16_t first_waiter;
    status_$t cleanup_status;
    uint8_t cleanup_context[24];
    ec2_waiter_t *waiter_table = (ec2_waiter_t *)EC2_WAITER_TABLE_BASE;

    *status_ret = status_$ok;

    cleanup_status = FIM_$CLEANUP(cleanup_context);
    if (cleanup_status == status_$cleanup_handler_set) {
        ML_$LOCK(EC2_LOCK_ID);

        ec_value = ec->value;
        waiter_idx = ec->awaiters;

        if (waiter_idx != 0) {
            if (waiter_idx > 0xE0 || waiter_table[waiter_idx].proc_id == 0) {
                *status_ret = status_$ec2_bad_event_count;
            } else {
                first_waiter = waiter_idx;
                do {
                    ec2_waiter_t *waiter = &waiter_table[waiter_idx];
                    waiter_idx = waiter->next;

                    if ((ec_value - waiter->wait_val) >= 0) {
                        int16_t ec1_offset = waiter->proc_id * 0x0C;
                        ec_$eventcount_t *ec1 = (ec_$eventcount_t *)(EC1_ARRAY_BASE + ec1_offset);
                        EC_$ADVANCE(ec1);
                    }
                } while (waiter_idx != first_waiter);
            }
        }

        ML_$UNLOCK(EC2_LOCK_ID);
        FIM_$RLS_CLEANUP(cleanup_context);
    } else {
        ML_$UNLOCK(EC2_LOCK_ID);
        FIM_$SIGNAL(cleanup_status);
    }
}
