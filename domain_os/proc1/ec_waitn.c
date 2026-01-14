/*
 * PROC1_$EC_WAITN - Wait on multiple event counts (internal)
 * Original address: 0x00e2065a
 *
 * This function waits for any of a set of event counts to reach
 * specified values. It maintains waiter lists and handles the
 * scheduling when waits complete.
 *
 * Note: This is a low-level function primarily implemented in assembly.
 * The C version here captures the algorithm but the original uses
 * careful stack manipulation for the waiter structures.
 *
 * Parameters:
 *   pcb - Process control block of waiting process
 *   ecs - Array of pointers to event counts
 *   wait_vals - Array of values to wait for
 *   num_ecs - Number of event counts to wait on
 *
 * Returns:
 *   Index of the event count that was satisfied (1-based), or 0 on error
 */

#include "proc1.h"

/* External TIME_$CLOCKH - current time high word */
#if defined(M68K)
    #define TIME_$CLOCKH    (*(uint32_t*)0xe2b0d4)
#else
    extern uint32_t TIME_$CLOCKH;
#endif

/*
 * Event count waiter structure
 * This is built on the stack for each event count being waited on
 */
typedef struct ec_waiter_t {
    struct ec_waiter_t *next_waiter;    /* Next waiter in EC's list */
    struct ec_waiter_t *prev_waiter;    /* Previous waiter in EC's list */
    proc1_t *pcb;                        /* PCB of waiting process */
    uint32_t wait_val;                   /* Value being waited for */
} ec_waiter_t;

/* External functions */
extern void proc1_$remove_from_ready_list(proc1_t *pcb);
extern void PROC1_$DISPATCH_INT2(proc1_t *pcb);

uint16_t PROC1_$EC_WAITN(proc1_t *pcb, ec_$eventcount_t **ecs,
                          int32_t *wait_vals, int16_t num_ecs)
{
    int16_t i;
    int16_t satisfied_idx;
    ec_waiter_t waiters[16];  /* Max 16 event counts */
    ec_$eventcount_t *ec;
    int32_t wait_val;
    int32_t ec_val;
    uint16_t saved_sr;

    DISABLE_INTERRUPTS(saved_sr);

    /* Check for no event counts */
    if (num_ecs <= 0) {
        ENABLE_INTERRUPTS(saved_sr);
        return 1;  /* Return success with index 1 */
    }

    /* Set up waiters for each event count */
    for (i = 0; i < num_ecs; i++) {
        ec = ecs[i];
        wait_val = wait_vals[i];

        /* Initialize waiter structure */
        waiters[i].next_waiter = (ec_waiter_t*)ec;  /* Point to EC (treated as list head) */
        waiters[i].wait_val = wait_val;
        waiters[i].pcb = pcb;

        /* Insert into EC's waiter list (before head) */
        waiters[i].prev_waiter = (ec_waiter_t*)((char*)ec + 4);  /* EC's prev pointer */

        /* Link into list */
        /* This would manipulate the EC's linked list */

        /* Check if already satisfied */
        ec_val = ec->value;
        if ((wait_val - ec_val) <= 0) {
            /* Already satisfied - don't need to wait */
            continue;
        }

        /* Need to wait - remove from ready list and block */
        proc1_$remove_from_ready_list(pcb);
        pcb->pri_max |= PROC1_FLAG_WAITING;
        pcb->wait_start = TIME_$CLOCKH;
        PROC1_$DISPATCH_INT2(pcb);
    }

    /* Check which event count was satisfied */
    satisfied_idx = 0xFFFF;
    for (i = num_ecs - 1; i >= 0; i--) {
        /* Remove from waiter list */
        ec_waiter_t *w = &waiters[i];
        w->prev_waiter->next_waiter = w->next_waiter;
        w->next_waiter->prev_waiter = w->prev_waiter;

        /* Check if this one was satisfied */
        ec = ecs[i];
        if ((w->wait_val - ec->value) <= 0) {
            satisfied_idx = i;
        }
    }

    ENABLE_INTERRUPTS(saved_sr);

    return satisfied_idx + 1;  /* Return 1-based index */
}
