/*
 * TIME_$RELEASE - Release timer resources
 *
 * Called when a process exits to release all timer resources.
 * Removes any pending timers from the RTE and VT queues.
 *
 * Original address: 0x00e58b58
 */

#include "time/time_internal.h"

void TIME_$RELEASE(void)
{
    status_$t status;
    uint8_t *itimer_entry;
    int16_t as_offset;
    time_queue_t *vt_queue;

    /* Calculate offset for this AS in the itimer database */
    as_offset = PROC1_$AS_ID * ITIMER_DB_ENTRY_SIZE;
    itimer_entry = (uint8_t *)ARCH_VA_TO_PTR(ITIMER_DB_BASE + as_offset);

    /* Remove real-time itimer from RTE queue */
    TIME_$Q_REMOVE_ELEM(&TIME_$RTEQ,
                        (time_queue_elem_t *)itimer_entry,
                        &status);

    /* Clear the real-time interval */
    *(uint32_t *)(itimer_entry + ITIMER_REAL_INTERVAL_HIGH) = 0;
    *(uint16_t *)(itimer_entry + ITIMER_REAL_INTERVAL_LOW) = 0;

    /*
     * 0xE58BBE..0xE58BCC: movea.l #0xe2a4a0,A2 / pea (-0xc,A2,D1w*0x1) with
     * D1 = PROC1_$CURRENT*12 - the Pascal 1-based virtual timer queue array.
     */
    vt_queue = &TIME_$VTQ[PROC1_$CURRENT - 1];

    /* Remove virtual itimer from VT queue */
    TIME_$Q_REMOVE_ELEM(vt_queue,
                        (time_queue_elem_t *)(itimer_entry +
                                              ITIMER_VIRT_ELEM_OFFSET),
                        &status);

    /* Clear the virtual interval */
    *(uint32_t *)(itimer_entry + ITIMER_VIRT_INTERVAL_HIGH) = 0;
    *(uint16_t *)(itimer_entry + ITIMER_VIRT_INTERVAL_LOW) = 0;
}
