/*
 * PROC1_$GET_LIST - Get list of bound processes
 * Original address: 0x00e15362
 *
 * Returns a list of all bound processes with ASID == 0 (kernel processes?).
 * For each matching process, returns PID and type.
 *
 * Parameters:
 *   count_ret - Pointer to receive count of processes found
 *   list_ret - Array to receive process info (4 bytes per entry:
 *              2 bytes PID, 2 bytes type)
 */

#include "proc1.h"

/*
 * Process list entry structure (4 bytes)
 */
typedef struct proc_list_entry_t {
    uint16_t pid;       /* Process ID */
    uint16_t type;      /* Process type */
} proc_list_entry_t;

void PROC1_$GET_LIST(int16_t *count_ret, proc_list_entry_t *list_ret)
{
    proc1_t *pcb;
    int16_t count;
    uint16_t i;

    *count_ret = 0;

    /* Scan all possible PIDs (0-64) */
    for (i = 0; i <= 0x40; i++) {
        pcb = PCBS[i];

        /* Check if PCB exists */
        if (pcb == NULL) {
            continue;
        }

        /* Check if bound (bit 3 of pri_max) */
        if ((pcb->pri_max & PROC1_FLAG_BOUND) == 0) {
            continue;
        }

        /* Check if ASID is 0 (kernel process) */
        if (pcb->asid != 0) {
            continue;
        }

        /* Add to list */
        count = ++(*count_ret);
        list_ret[count - 1].pid = pcb->mypid;
        list_ret[count - 1].type = PROC1_$TYPE[pcb->mypid];
    }
}
