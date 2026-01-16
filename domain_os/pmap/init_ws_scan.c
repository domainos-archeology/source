/*
 * PMAP_$INIT_WS_SCAN - Initialize working set scanning for a process
 *
 * Sets up periodic working set scanning for a process. Creates a
 * timer callback that will periodically scan the working set and
 * age pages for the clock page replacement algorithm.
 *
 * Parameters:
 *   index - Working set index (0-63)
 *   param - Initial parameter value
 *
 * Original address: 0x00e145f0
 */

#include "pmap_internal.h"

/*
 * Timer queue and element arrays for working set scanning.
 * Each working set slot has its own timer queue and element.
 *
 * Original locations (M68K):
 *   PMAP_$WS_TIMER_QUEUES:   0xE2A494
 *   PMAP_$WS_TIMER_ELEMENTS: 0xE24D68
 */
extern time_queue_t PMAP_$WS_TIMER_QUEUES[];
extern time_queue_elem_t PMAP_$WS_TIMER_ELEMENTS[];

/* Verify structure sizes match original layout */
_Static_assert(sizeof(time_queue_t) == 0x0C,
               "time_queue_t size must be 0x0C bytes");
_Static_assert(sizeof(time_queue_elem_t) == 0x1A,
               "time_queue_elem_t size must be 0x1A bytes");

/* Working set scan callback - declared in pmap_internal.h */
extern void PMAP_$WS_SCAN_CALLBACK(int *arg);

void PMAP_$INIT_WS_SCAN(uint16_t index, int16_t param)
{
    uint16_t local_param;
    status_$t status;
    time_queue_t *queue;
    time_queue_elem_t *elem;

    local_param = (uint16_t)param;

    /* Set up working set index */
    MMAP_$SET_WS_INDEX(index, &local_param);

    /* Skip timer setup for slot 5 (special slot) */
    if (param == 5) {
        return;
    }

    /* Get pointers to this slot's queue and element */
    queue = &PMAP_$WS_TIMER_QUEUES[index];
    elem = &PMAP_$WS_TIMER_ELEMENTS[index];

    /* Remove any existing timer entry */
    TIME_$Q_REMOVE_ELEM(queue, elem, &status);

    /* Set up new timer entry */
    /* Flags: 0x1A (repeating timer) */
    elem->flags = 0x1A;

    /* Initial expiration time: 0 (fire immediately on next scan) */
    elem->expire_high = 0;
    elem->expire_low = 0;

    /* Interval: 250000 microseconds (250ms) */
    elem->interval_high = 250000;
    elem->interval_low = 0;

    /* Set callback function and parameter */
    elem->callback = (uint32_t)(uintptr_t)PMAP_$WS_SCAN_CALLBACK;
    elem->callback_arg = (uint32_t)index;

    /* Enter timer in queue */
    {
        clock_t when = { 0, 0 };
        TIME_$Q_ENTER_ELEM(queue, &when, elem, &status);
    }
}
