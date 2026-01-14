/*
 * PROC1_$GET_INFO - Get process information
 * Original address: 0x00e14f52
 *
 * Returns information about a process including state, CPU time,
 * and register values.
 *
 * Parameters:
 *   pidp - Pointer to process ID
 *   info_ret - Pointer to info structure to fill
 *   status_ret - Status return pointer
 */

#include "proc1.h"

/*
 * Process info structure returned by PROC1_$GET_INFO
 * Size appears to be at least 0x18 bytes
 */
typedef struct proc1_$info_t {
    int16_t     state;          /* 0x00: Process state */
    uint16_t    usr;            /* 0x02: User status register */
    uint32_t    upc;            /* 0x04: User PC */
    uint32_t    usp;            /* 0x08: User stack pointer */
    uint16_t    usb;            /* 0x0C: User stack base? */
    uint16_t    pad_0e;         /* 0x0E: Padding */
    uint8_t     cpu_total[8];   /* 0x10: CPU time (6 bytes used) */
} proc1_$info_t;

/* External function to get info from interrupt context */
extern void PROC1_$GET_INFO_INT(uint16_t pid, void *stack_base, void *stack_top,
                                 uint16_t *usr, uint32_t *upc, uint16_t *usb, uint32_t *usp);

void PROC1_$GET_INFO(int16_t *pidp, proc1_$info_t *info_ret, status_$t *status_ret)
{
    uint16_t pid;
    proc1_t *pcb;
    void *stack;

    pid = *pidp;
    *status_ret = status_$ok;

    /* Validate PID */
    if (pid == 0 || pid > 0x40) {
        *status_ret = status_$illegal_process_id;
        return;
    }

    /* Get PCB */
    pcb = PCBS[pid];
    if (pcb == NULL) {
        *status_ret = status_$illegal_process_id;
        return;
    }

    /* Check if bound */
    if ((pcb->pri_max & PROC1_FLAG_BOUND) == 0) {
        *status_ret = status_$process_not_bound;
        return;
    }

    /* Copy state (from pri_min:pri_max word, shifted to get state byte) */
    info_ret->state = pcb->state;

    /* Copy current CPU time */
    *(uint32_t*)&info_ret->cpu_total[0] = pcb->cpu_total;
    *(uint32_t*)&info_ret->cpu_total[4] = pcb->cpu_usage;

    /* Add current accumulated time (48-bit addition) */
    ADD48(info_ret->cpu_total, &pcb->cpu_total);

    /* If this is the current process, we're done */
    if (pcb == PROC1_$CURRENT_PCB) {
        return;
    }

    /* Get register info from stack for non-current processes */
    stack = OS_STACK_BASE[pid];
    if (stack != NULL) {
        /* Call internal function to extract register info from stack
         * Stack base is stack - 0x1000 (4KB stack)
         */
        PROC1_$GET_INFO_INT(pid,
                            (char*)stack - 0x1000,  /* Stack base */
                            stack,                   /* Stack top */
                            &info_ret->usr,
                            &info_ret->upc,
                            &info_ret->usb,
                            &info_ret->usp);
    } else {
        /* No stack, clear register fields */
        info_ret->usr = 0;
        info_ret->upc = 0;
        info_ret->usp = 0;
        info_ret->usb = 0;
    }
}
