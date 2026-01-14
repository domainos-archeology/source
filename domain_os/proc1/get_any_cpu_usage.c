/*
 * PROC1_$GET_ANY_CPU_USAGE - Get CPU usage statistics for any process
 * Original address: 0x00e1543e
 *
 * Returns CPU time and additional statistics for a specified process.
 *
 * Parameters:
 *   pid_ptr - Pointer to process ID
 *   cpu_time_ret - Pointer to receive CPU time (6 bytes)
 *   stat1_ret - Pointer to receive statistic at PCB+0x60
 *   stat2_ret - Pointer to receive statistic at PCB+0x64
 *
 * Note: Crashes system if PID is invalid.
 */

#include "proc1.h"

/* External crash function */
extern void CRASH_SYSTEM(const char *msg);

static const char ILLEGAL_PID_MSG[] = "Illegal process id";

void PROC1_$GET_ANY_CPU_USAGE(uint16_t *pid_ptr, void *cpu_time_ret,
                               uint32_t *stat1_ret, uint32_t *stat2_ret)
{
    proc1_t *pcb;
    uint16_t pid;
    uint32_t *time_out = (uint32_t*)cpu_time_ret;

    pid = *pid_ptr;

    /* Validate PID - crash on invalid */
    if (pid == 0 || pid > 0x40) {
        CRASH_SYSTEM(ILLEGAL_PID_MSG);
        return;
    }

    /* Get PCB */
    pcb = PCBS[pid];

    /* Copy base CPU time (6 bytes) */
    time_out[0] = pcb->cpu_total;
    ((uint16_t*)cpu_time_ret)[2] = pcb->cpu_usage;

    /* Add current accumulated time (48-bit addition) */
    ADD48(cpu_time_ret, &pcb->cpu_total);

    /* Return additional statistics from PCB */
    *stat1_ret = pcb->field_60;
    *stat2_ret = pcb->field_64;
}
