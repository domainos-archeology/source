/*
 * PROC1_$SET_TS - Set timeslice for a process
 * Original address: 0x00e14a08
 *
 * Schedules a timeslice timer callback for a process. Uses the kernel's
 * timer queue mechanism to trigger PROC1_$TS_END_CALLBACK when the
 * timeslice expires.
 *
 * Parameters:
 *   pcb - Process control block pointer
 *   timeslice - Timeslice value (in timer ticks)
 */

#include "proc1.h"

/* External timer queue re-enter function */
extern void TIME_$Q_REENTER_ELEM(void *queue_elem, void *time, uint16_t flags,
                                  void *dest_time, void *callback_info,
                                  status_$t *status);

/* Size of timer queue element (12 bytes) */
#define TS_QUEUE_ELEM_SIZE  12

void PROC1_$SET_TS(proc1_t *pcb, int16_t timeslice)
{
    status_$t status;
    uint32_t time_high;
    uint16_t time_low;
    uint16_t pid;
    char *queue_elem;
    char *callback_info;

    pid = pcb->mypid;

    /* Build time value (6-byte: 4 high + 2 low) */
    time_high = 0;
    time_low = timeslice;

    /*
     * Calculate timer table entry address:
     * entry = base + pid * 0x1c (28 bytes per entry)
     * The assembly computes pid * 28 using shifts:
     * D0 = pid * 4, D1 = pid * 4, D0 = -D0
     * D1 <<= 3 => pid * 32, D0 += D1 => -4 + 32 = 28
     *
     * callback_info points to offset 0x14 within the entry
     */
    callback_info = (char*)&TS_TIMER_TABLE[pid] + 0x14;

    /* Calculate queue element address: base + pid * 12 - 0xC */
    queue_elem = &TS_QUEUE_TABLE[pid * TS_QUEUE_ELEM_SIZE] - 0xC;

    /* Schedule the timer callback */
    TIME_$Q_REENTER_ELEM(queue_elem, &time_high, 0, &pcb->cpu_total,
                         callback_info, &status);
}
