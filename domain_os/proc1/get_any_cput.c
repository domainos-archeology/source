/*
 * PROC1_$GET_ANY_CPUT - Get CPU time for any process
 * Original address: 0x00e153f8
 *
 * Returns the accumulated CPU time for a specified process.
 * Unlike PROC1_$GET_CPUT, this can get time for any process,
 * not just the current one.
 *
 * Parameters:
 *   cpu_time_ret - Pointer to receive CPU time (6 bytes)
 *   pid - Process ID
 *
 * Note: Crashes system if PID is invalid.
 */

#include "proc1.h"

/* External crash function */
extern void CRASH_SYSTEM(const char *msg);

static const char ILLEGAL_PID_MSG[] = "Illegal process id";

void PROC1_$GET_ANY_CPUT(void *cpu_time_ret, uint16_t pid)
{
    proc1_t *pcb;
    uint32_t *time_out = (uint32_t*)cpu_time_ret;

    /* Validate PID - crash on invalid */
    if (pid == 0 || pid > 0x40) {
        CRASH_SYSTEM(ILLEGAL_PID_MSG);
        return;
    }

    /* Get PCB */
    pcb = PCBS[pid];

    /* Copy CPU time (6 bytes: 4-byte high + 2-byte low) */
    time_out[0] = pcb->cpu_total;
    ((uint16_t*)cpu_time_ret)[2] = pcb->cpu_usage;
}
