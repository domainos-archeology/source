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

#include "proc1/proc1_internal.h"
#include "misc/misc.h"

/*
 * Status cells passed to CRASH_SYSTEM by `pea (d,PC)`.
 *
 * These are constant longwords in this module's own code region, not
 * shared globals; the cell address is part of each name.  Names come from
 * the SR10.4 status-code database.
 */
/*
 * 0x00E1540C: pea (-0x12e,PC) -> 0x00E152E0, jsr CRASH_SYSTEM at 0x00E15410.
 * Shared with PROC1_$SET_PRIORITY and PROC1_$GET_ANY_CPU_USAGE.
 */
static const status_$t proc1_$illegal_process_id_00e152e0 = 0x000A0001;

void PROC1_$GET_ANY_CPUT(void *cpu_time_ret, uint16_t pid)
{
    proc1_t *pcb;
    uint32_t *time_out = (uint32_t*)cpu_time_ret;

    /* Validate PID - crash on invalid */
    if (pid == 0 || pid > 0x40) {
        CRASH_SYSTEM(&proc1_$illegal_process_id_00e152e0);
        return;
    }

    /* Get PCB */
    pcb = PCBS[pid];

    /* Copy CPU time (6 bytes: 4-byte high + 2-byte low) */
    time_out[0] = pcb->cpu_total;
    ((uint16_t*)cpu_time_ret)[2] = pcb->cpu_usage;
}
