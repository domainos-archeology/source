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

#include "pmap.h"

/* Timer queue entry structure */
typedef struct timer_entry_t {
    void    (*callback)(int *);     /* 0x00: Callback function */
    uint32_t param;                 /* 0x04: Callback parameter */
    uint16_t flags;                 /* 0x08: Timer flags */
    uint16_t interval_lo;           /* 0x0A: Interval low word */
    uint32_t interval;              /* 0x0C: Interval in microseconds */
    uint16_t next_lo;               /* 0x10: Next fire time low */
    uint16_t next_hi;               /* 0x12: Next fire time high */
    uint16_t pad;                   /* 0x14: Padding */
} timer_entry_t;

/* Timer entry array base - one per slot */
#if defined(M68K)
    #define TIMER_ENTRIES_BASE      0xE24D68
#else
    extern timer_entry_t timer_entries[];
    #define TIMER_ENTRIES_BASE      ((uintptr_t)timer_entries)
#endif

/* Timer queue base */
#if defined(M68K)
    #define TIMER_QUEUE_BASE        0xE2A494
#else
    extern uint8_t timer_queue[];
    #define TIMER_QUEUE_BASE        ((uintptr_t)timer_queue)
#endif

void PMAP_$INIT_WS_SCAN(uint16_t index, int16_t param)
{
    int16_t local_param;
    status_$t status[2];
    uint16_t dummy;
    int entry_offset;

    local_param = param;

    /* Set up working set index */
    MMAP_$SET_WS_INDEX(index, &local_param);

    /* Skip timer setup for slot 5 (special slot) */
    if (param == 5) {
        return;
    }

    /* Calculate timer entry offset */
    entry_offset = (int16_t)(index * 0x1C);

    /* Remove any existing timer entry */
    TIME_$Q_REMOVE_ELEM((int16_t)(index * 0x0C) + TIMER_QUEUE_BASE,
                        index * 0x1C + 0x4D68, (int16_t)status);

    /* Set up new timer entry */
    timer_entry_t *entry = (timer_entry_t *)(TIMER_ENTRIES_BASE + entry_offset);

    /* Timer flags: 0x1A */
    *(uint16_t *)((char *)entry + 0x12) = 0x1A;
    *(uint16_t *)((char *)entry + 0x0C) = 0;

    /* Interval: 250000 microseconds (250ms) */
    *(uint32_t *)((char *)entry + 0x0E) = 250000;

    /* Copy interval to next fire time */
    *(uint32_t *)((char *)entry + 0x12) = *(uint32_t *)((char *)entry + 0x0C);
    *(uint16_t *)((char *)entry + 0x18) = *(uint16_t *)((char *)entry + 0x10);

    /* Set callback function and parameter */
    entry->callback = PMAP_$WS_SCAN_CALLBACK;
    entry->param = (uint32_t)index;

    /* Enter timer in queue */
    status[1] = 0;
    dummy = 0;
    TIME_$Q_ENTER_ELEM((int16_t)(index * 0x0C) + TIMER_QUEUE_BASE,
                       &status[1], (void *)(TIMER_ENTRIES_BASE + entry_offset), status);
}
