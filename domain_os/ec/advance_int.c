/*
 * ADVANCE_INT - Internal advance function
 *
 * Increments the eventcount value and wakes any waiters whose
 * wait value has been reached. Called with interrupts disabled.
 *
 * The waiter list is a circular doubly-linked list with the
 * eventcount structure itself as the sentinel.
 *
 * Parameters:
 *   ec - Pointer to eventcount (passed in A0 on m68k)
 *
 * Original address: 0x00e2072c
 */

#include "ec.h"

/*
 * Process control block offsets for priority and wait management
 */
#define PCB_STATE_OFFSET        0x52
#define PCB_PRI_MAX_OFFSET      0x55
#define PCB_INH_COUNT_OFFSET    0x58
#define PCB_WAIT_START_OFFSET   0x3C

void ADVANCE_INT(ec_$eventcount_t *ec)
{
    int32_t new_value;
    ec_$eventcount_waiter_t *waiter;
    uint8_t *pcb;
    uint8_t pri_flags;
    uint32_t wait_time;
    int16_t new_pri;
    int16_t max_pri;

    /* Increment the eventcount value */
    new_value = ec->value + 1;
    ec->value = new_value;

    /* Walk the waiter list */
    for (waiter = ec->waiter_list_head;
         (ec_$eventcount_t *)waiter != ec;
         waiter = (ec_$eventcount_waiter_t *)waiter->prev_waiter) {

        /* Check if this waiter should be woken */
        if ((new_value - waiter->wait_val) >= 0) {
            pcb = (uint8_t *)waiter->pcb;

            /* Clear the "waiting" flag (bit 0 of pri_max) */
            pri_flags = pcb[PCB_PRI_MAX_OFFSET];
            pcb[PCB_PRI_MAX_OFFSET] = pri_flags & 0xFE;

            /* If the process was waiting (bit 0 was set) */
            if ((pri_flags & 1) != 0) {
                /* Calculate time spent waiting */
                wait_time = TIME_$CLOCKH - *(uint32_t *)(pcb + PCB_WAIT_START_OFFSET);

                if (wait_time != 0) {
                    /* Adjust priority based on wait time */
                    max_pri = *(int16_t *)(pcb + PCB_INH_COUNT_OFFSET);

                    if (wait_time < 0x12) {
                        /* Short wait - add to current priority */
                        new_pri = *(int16_t *)(pcb + PCB_STATE_OFFSET) + (int16_t)wait_time;
                        if (new_pri <= max_pri) {
                            *(int16_t *)(pcb + PCB_STATE_OFFSET) = new_pri;
                            PROC1_$SET_TS(waiter->pcb, (int16_t)(new_value >> 8));
                        } else {
                            *(int16_t *)(pcb + PCB_STATE_OFFSET) = max_pri;
                        }
                    } else {
                        /* Long wait - use max priority */
                        if (*(int16_t *)(pcb + PCB_STATE_OFFSET) < max_pri) {
                            *(int16_t *)(pcb + PCB_STATE_OFFSET) = max_pri;
                            PROC1_$SET_TS(waiter->pcb, (int16_t)(new_value >> 8));
                        } else {
                            *(int16_t *)(pcb + PCB_STATE_OFFSET) = max_pri;
                        }
                    }
                }

                /* If process is not inhibited (bit 1 not set), wake it */
                if ((pcb[PCB_PRI_MAX_OFFSET] & 0x02) == 0) {
                    FUN_00e20844();
                }
            }
        }
    }
}
