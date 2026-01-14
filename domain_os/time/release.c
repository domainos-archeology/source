/*
 * TIME_$RELEASE - Release timer resources
 *
 * Called when a process exits to release all timer resources.
 * Removes any pending timers from the RTE and VT queues.
 *
 * Original address: 0x00e58b58
 */

#include "time.h"
#include "proc1/proc1.h"

/* External references */
extern uint16_t PROC1_$AS_ID;
extern uint16_t PROC1_$CURRENT;

/* itimer database base - 28 bytes per AS */
#define ITIMER_DB_BASE      0xE297F0
#define ITIMER_DB_ENTRY_SIZE 0x1C  /* 28 bytes */

/* Offsets within itimer entry */
#define ITIMER_REAL_INTERVAL_HIGH   0x0C
#define ITIMER_REAL_INTERVAL_LOW    0x10
#define ITIMER_VIRT_INTERVAL_HIGH   0x664
#define ITIMER_VIRT_INTERVAL_LOW    0x668

/* VT queue array base */
#define VT_QUEUE_ARRAY_BASE 0xE2A494

void TIME_$RELEASE(void)
{
    status_$t status;
    uint8_t *itimer_entry;
    int16_t as_offset;
    time_queue_t *vt_queue;

    /* Calculate offset for this AS in the itimer database */
    as_offset = PROC1_$AS_ID * ITIMER_DB_ENTRY_SIZE;
    itimer_entry = (uint8_t *)(ITIMER_DB_BASE + as_offset);

    /* Remove real-time itimer from RTE queue */
    TIME_$Q_REMOVE_ELEM(&TIME_$RTEQ,
                        (time_queue_elem_t *)itimer_entry,
                        &status);

    /* Clear the real-time interval */
    *(uint32_t *)(itimer_entry + ITIMER_REAL_INTERVAL_HIGH) = 0;
    *(uint16_t *)(itimer_entry + ITIMER_REAL_INTERVAL_LOW) = 0;

    /* Calculate VT queue for current process */
    vt_queue = (time_queue_t *)(VT_QUEUE_ARRAY_BASE +
                                (PROC1_$CURRENT * 12) - 12);

    /* Remove virtual itimer from VT queue */
    TIME_$Q_REMOVE_ELEM(vt_queue,
                        (time_queue_elem_t *)(itimer_entry + 0x658),
                        &status);

    /* Clear the virtual interval */
    *(uint32_t *)(itimer_entry + ITIMER_VIRT_INTERVAL_HIGH) = 0;
    *(uint16_t *)(itimer_entry + ITIMER_VIRT_INTERVAL_LOW) = 0;
}
