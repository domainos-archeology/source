/*
 * EC2_$WAIT - Wait on multiple EC2s
 *
 * Complex wait function supporting both direct pointers and
 * index-based EC2s. Sets up waiter entries and delegates to
 * EC_$WAITN for the actual wait.
 *
 * Parameters:
 *   ec - Array of EC2 pointers/indices
 *   wait_vals - Array of wait values
 *   count - Pointer to count of ECs (max 128)
 *   status_ret - Status return
 *
 * Returns:
 *   Index of satisfied EC2 (0 = first, etc.)
 *
 * Original address: 0x00e42358
 */

#include "ec.h"
#include "proc1/proc1.h"
#include "ml/ml.h"

extern uint32_t _DAT_00e7cf04;
extern void *DAT_00e7caf8[];
extern uint16_t DAT_00e7cf08;
extern uint32_t DAT_00e7cefc;
extern void *AS_$PROTECTION;

/* Maximum number of ECs to wait on */
#define MAX_WAIT_COUNT  128

int16_t EC2_$WAIT(ec2_$eventcount_t *ec, int32_t *wait_vals,
                  int16_t *count, status_$t *status_ret)
{
    int16_t num_ecs;
    int16_t result = 0;
    status_$t cleanup_status;
    uint8_t cleanup_context[24];

    num_ecs = *count;
    if (num_ecs > MAX_WAIT_COUNT) {
        *status_ret = 0x00180001;  /* Too many ECs */
        return 0;
    }

    *status_ret = status_$ok;

    cleanup_status = FIM_$CLEANUP(cleanup_context);
    if (cleanup_status == status_$cleanup_handler_set) {
        ec_$eventcount_t *ec1_array[32];
        int32_t ec1_wait_vals[32];
        uint16_t waiter_indices[128];
        int16_t ec1_count = 2;  /* Start at 2 (0=proc ec, 1=quit ec) */
        int16_t satisfied = -1;
        int16_t i;

        ML_$LOCK(EC2_LOCK_ID);

        /* Main wait loop */
        do {
            /* Process each EC2 */
            for (i = 0; i < num_ecs && satisfied < 0; i++) {
                ec2_$eventcount_t *cur_ec = &ec[i];
                uintptr_t ec_val = (uintptr_t)cur_ec->value;

                waiter_indices[i] = 0;

                if (ec_val == 0) {
                    *status_ret = status_$ec2_bad_event_count;
                    satisfied = i + 1;
                    break;
                }

                if (ec_val == 1) {
                    /* Special: already satisfied */
                    satisfied = i + 1;
                    break;
                }

                if (ec_val <= _DAT_00e7cf04) {
                    /* Registered EC1 */
                    ec_$eventcount_t *ec1 = (ec_$eventcount_t *)DAT_00e7caf8[ec_val];
                    if (ec1_count < 32) {
                        ec1_array[ec1_count] = ec1;
                        ec1_wait_vals[ec1_count] = wait_vals[i];
                        ec1_count++;
                    }
                } else if (ec_val >= 0x101 && ec_val <= 0x120) {
                    /* PBU pool EC1 */
                    uint16_t pbu_index = (uint16_t)(ec_val - 0x101);
                    if ((DAT_00e7cf00 & (1 << (pbu_index & 0x1F))) == 0) {
                        *status_ret = status_$ec2_bad_event_count;
                        satisfied = i + 1;
                        break;
                    }
                    ec_$eventcount_t *ec1 = (ec_$eventcount_t *)
                        (EC2_PBU_ECS_BASE + pbu_index * 0x18);
                    /* Increment reference count */
                    int16_t *refcount = (int16_t *)((uint8_t *)ec1 + 0x0E);
                    (*refcount)++;

                    if (ec1_count < 32) {
                        ec1_array[ec1_count] = ec1;
                        ec1_wait_vals[ec1_count] = wait_vals[i];
                        ec1_count++;
                    }
                } else if (ec_val > 0x3E8) {
                    /* Direct pointer mode - allocate waiter entry */
                    ec2_$eventcount_t *direct_ec = (ec2_$eventcount_t *)ec_val;

                    if ((uintptr_t)direct_ec >= (uintptr_t)AS_$PROTECTION) {
                        *status_ret = status_$fault_protection_boundary_violation;
                        satisfied = i + 1;
                        break;
                    }

                    uint16_t old_awaiter = direct_ec->awaiters;
                    direct_ec->awaiters = 0xFFFF;  /* Mark as being modified */

                    uint16_t free_idx = DAT_00e7cf08;
                    if (free_idx == 0) {
                        direct_ec->awaiters = old_awaiter;
                        *status_ret = 0x00180001;  /* No free waiter entries */
                        satisfied = i + 1;
                        break;
                    }

                    /* Allocate waiter entry from free list */
                    ec2_waiter_t *waiter_table = (ec2_waiter_t *)EC2_WAITER_TABLE_BASE;
                    ec2_waiter_t *waiter = &waiter_table[free_idx];
                    DAT_00e7cf08 = waiter->next;

                    /* Link into list */
                    if (old_awaiter == 0) {
                        waiter->next = free_idx;
                        waiter->prev = free_idx;
                    } else {
                        waiter->prev = old_awaiter;
                        waiter->next = waiter_table[old_awaiter].next;
                        waiter_table[waiter->next].prev = free_idx;
                        waiter_table[old_awaiter].next = free_idx;
                    }

                    waiter->proc_id = PROC1_$CURRENT;
                    waiter->wait_val = wait_vals[i];
                    waiter_indices[i] = free_idx;
                    direct_ec->awaiters = old_awaiter ? old_awaiter : free_idx;

                    /* Check if already satisfied */
                    if ((direct_ec->value - wait_vals[i]) >= 0) {
                        satisfied = i + 1;
                    }
                } else {
                    *status_ret = status_$ec2_bad_event_count;
                    satisfied = i + 1;
                    break;
                }
            }

            if (satisfied >= 0) break;

            /* Set up process EC for wait */
            int16_t proc_offset = PROC1_$CURRENT * 0x0C;
            ec1_array[0] = (ec_$eventcount_t *)(EC1_ARRAY_BASE + proc_offset);
            ec1_wait_vals[0] = ec1_array[0]->value + 1;

            /* Set up quit EC */
            int16_t as_offset = PROC1_$AS_ID * 0x0C;
            ec1_array[1] = &FIM_$QUIT_EC;
            ec1_wait_vals[1] = FIM_$QUIT_VALUE + 1;

            ML_$UNLOCK(EC2_LOCK_ID);

            /* Actually wait */
            uint16_t wait_result = EC_$WAITN(ec1_array, ec1_wait_vals, ec1_count);

            if (wait_result == 2) {
                /* Quit EC signaled */
                FIM_$QUIT_VALUE = FIM_$QUIT_EC.value;
                *status_ret = status_$ec2_async_fault_while_waiting;
                satisfied = 0;
            }

            ML_$LOCK(EC2_LOCK_ID);

            /* Clean up waiter entries */
            for (i = num_ecs - 1; i >= 0 && satisfied < 0; i--) {
                uint16_t widx = waiter_indices[i];
                if (widx != 0) {
                    ec2_waiter_t *waiter_table = (ec2_waiter_t *)EC2_WAITER_TABLE_BASE;
                    ec2_waiter_t *waiter = &waiter_table[widx];

                    /* Unlink from list */
                    if (waiter->next != widx) {
                        waiter_table[waiter->prev].next = waiter->next;
                        waiter_table[waiter->next].prev = waiter->prev;
                    }

                    /* Return to free list */
                    waiter->next = DAT_00e7cf08;
                    waiter->proc_id = 0;
                    DAT_00e7cf08 = widx;
                    waiter_indices[i] = 0;

                    /* Check if satisfied */
                    ec2_$eventcount_t *cur_ec = &ec[i];
                    if ((cur_ec->value - wait_vals[i]) >= 0) {
                        satisfied = i + 1;
                    }
                } else {
                    /* Index-based EC - check satisfaction and decrement refcount */
                    uintptr_t ec_val = (uintptr_t)ec[i].value;
                    if (ec_val >= 0x101 && ec_val <= 0x120) {
                        uint16_t pbu_index = (uint16_t)(ec_val - 0x101);
                        ec_$eventcount_t *ec1 = (ec_$eventcount_t *)
                            (EC2_PBU_ECS_BASE + pbu_index * 0x18);
                        int16_t *refcount = (int16_t *)((uint8_t *)ec1 + 0x0E);
                        (*refcount)--;

                        if ((ec1->value - wait_vals[i]) >= 0) {
                            satisfied = i + 1;
                        }
                    }
                }
            }

        } while (satisfied < 0);

        result = satisfied;
        ML_$UNLOCK(EC2_LOCK_ID);
        FIM_$RLS_CLEANUP(cleanup_context);
    } else {
        /* Cleanup handler not set - signal error */
        ML_$UNLOCK(EC2_LOCK_ID);
        FIM_$SIGNAL(cleanup_status);
    }

    return result;
}
