/*
 * PROC1_$UNBIND - Unbind a process from its PCB
 * Original address: 0x00e14e24
 *
 * Releases a process's resources and frees its PCB slot.
 * Handles both current process (self-termination) and other processes.
 *
 * Parameters:
 *   pid - Process ID to unbind
 *   status_ret - Status return pointer
 */

#include "proc1.h"

/* OS stack table */
#if defined(M68K)
    #define OS_STACK_BASE   ((void**)0xe25c18)
#else
    extern void *OS_STACK_BASE[];
#endif

/* Suspend event count at 0xe205f6 */
#if defined(M68K)
    #define PROC1_$SUSPEND_EC   (*(uint32_t*)0xe205f6)
#else
    extern uint32_t PROC1_$SUSPEND_EC;
#endif

/* Timer queue base */
#if defined(M68K)
    #define TS_QUEUE_TABLE_BASE ((char*)0xe2a494)
#else
    extern char *TS_QUEUE_TABLE_BASE;
#endif

/* External functions */
extern void PMAP_$PURGE_WS(uint16_t pid, uint16_t param);
extern void TIME_$Q_FLUSH_QUEUE(void *queue_elem);
extern void EC_$WAIT(void *ecs[], uint32_t *wait_vals);
extern int8_t PROC1_$SUSPEND(uint16_t pid, status_$t *status_ret);
extern int8_t PROC1_$SUSPENDP(uint16_t pid, status_$t *status_ret);
extern void PROC1_$TRY_TO_SUSPEND(proc1_t *pcb);
extern void PROC1_$FREE_STACK(void *stack);
extern void PROC1_$SET_TYPE(uint16_t pid, uint16_t type);
extern void PROC1_$DISPATCH(void);
extern void CRASH_SYSTEM(void *msg);

/* Error message for failed self-suspend */
static const char UNBIND_CRASH_MSG[] = "PROC1_$UNBIND: self-suspend failed";

void PROC1_$UNBIND(uint16_t pid, status_$t *status_ret)
{
    proc1_t *pcb;
    int8_t suspend_result;
    uint32_t suspend_ec_val;
    uint32_t wait_val;
    void *ec_list[3];
    uint16_t saved_sr;
    char *queue_elem;

    /* Validate PID */
    if (pid == 0 || pid > 0x40) {
        *status_ret = status_$illegal_process_id;
        return;
    }

    /* Get PCB */
    pcb = PCBS[pid];

    /* Check if bound */
    if ((pcb->pri_max & PROC1_FLAG_BOUND) == 0) {
        *status_ret = status_$process_not_bound;
        return;
    }

    if (pcb == PROC1_$CURRENT_PCB) {
        /*
         * Self-termination case
         * Purge working set and suspend ourselves
         */
        PMAP_$PURGE_WS(pid, 0);

        DISABLE_INTERRUPTS(saved_sr);
        PROC1_$TRY_TO_SUSPEND(pcb);

        /* Verify we are now suspended */
        if ((pcb->pri_max & PROC1_FLAG_SUSPENDED) == 0) {
            /* Should not happen - crash the system */
            CRASH_SYSTEM((void*)UNBIND_CRASH_MSG);
        }
    } else {
        /*
         * Terminating another process
         * Need to wait for it to become suspended
         */
        if ((pcb->pri_max & PROC1_FLAG_SUSPENDED) == 0) {
            /* Get current suspend event count value */
            suspend_ec_val = PROC1_$SUSPEND_EC;

            /* Try to suspend */
            suspend_result = PROC1_$SUSPEND(pid, status_ret);

            /* Wait loop until process is suspended */
            while (suspend_result >= 0) {
                /* Wait for suspend event count to change */
                wait_val = suspend_ec_val + 1;
                ec_list[0] = (void*)&PROC1_$SUSPEND_EC;
                ec_list[1] = NULL;
                ec_list[2] = NULL;
                EC_$WAIT(ec_list, &wait_val);

                /* Check if now suspended */
                suspend_result = PROC1_$SUSPENDP(pid, status_ret);
            }
        }

        /* Purge working set */
        PMAP_$PURGE_WS(pid, 0);

        DISABLE_INTERRUPTS(saved_sr);
    }

    /* Flush the timer queue for this process */
    queue_elem = TS_QUEUE_TABLE_BASE + (pid * 12) - 12;
    TIME_$Q_FLUSH_QUEUE(queue_elem);

    /* Clear bound flag */
    pcb->pri_max &= ~PROC1_FLAG_BOUND;

    /* Free the process stack */
    PROC1_$FREE_STACK(OS_STACK_BASE[pid]);

    /* Clear process type */
    PROC1_$SET_TYPE(pid, 0);

    /* Dispatch to another process */
    PROC1_$DISPATCH();
}
