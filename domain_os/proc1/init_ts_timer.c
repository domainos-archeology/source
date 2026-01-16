/*
 * PROC1_$INIT_TS_TIMER - Initialize timeslice timer for a process
 * Original address: 0x00e14b12
 *
 * Sets up the timeslice timer infrastructure for a process. Creates the
 * timer entry with callback information and schedules the initial timer.
 *
 * Parameters:
 *   pid - Process ID to initialize timer for
 */

#include "proc1/proc1_internal.h"
#include "time/time.h"
#include "cal/cal.h"

#define TS_QUEUE_ELEM_SIZE      0x0c    /* 12 bytes per queue element */

void PROC1_$INIT_TS_TIMER(uint16_t pid)
{
    ts_timer_entry_t *entry;
    proc1_t *pcb;
    status_$t status;
    uint32_t time_high;
    uint16_t time_low;
    uint32_t delta_high;
    uint16_t delta_low;
    char *queue_elem;
    char *callback_info;

    /* Get timer entry for this pid */
    entry = &TS_TIMER_TABLE[pid];

    /* Clear field_26 */
    entry->field_26 = 0;

    /* Get PCB for this process */
    pcb = PCBS[pid];

    /* Copy current CPU time from PCB */
    time_high = pcb->cpu_total;
    time_low = pcb->cpu_usage;

    /* Build delta: add 0xFFFF (-1 as unsigned, i.e., max timeslice) */
    delta_high = 0;
    delta_low = 0xFFFF;

    /* Store base CPU time in entry */
    entry->cpu_time_high = time_high;
    entry->cpu_time_low = time_low;

    /* Add delta to get target time */
    ADD48((clock_t *)&entry->cpu_time_high, (clock_t *)&delta_high);

    /* Set up callback */
    entry->callback = PROC1_$TS_END_CALLBACK;
    entry->callback_param = pid;

    /* Calculate addresses */
    callback_info = (char*)entry + 0x14;  /* Offset to callback_info field */
    queue_elem = &TS_QUEUE_TABLE[pid * TS_QUEUE_ELEM_SIZE] - 0xC;

    /* Enter timer into queue */
    TIME_$Q_ENTER_ELEM((time_queue_t *)queue_elem, (clock_t *)&time_high,
                       (time_queue_elem_t *)callback_info, &status);
}
