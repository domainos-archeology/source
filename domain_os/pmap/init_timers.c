/*
 * PMAP_$INIT_TIMERS - Initialize PMAP purifier and update timers
 *
 * Sets up two periodic timer callbacks in the real-time event queue (TIME_$RTEQ):
 *
 * 1. Purifier timer (PMAP_$T_PURIF_CALLBACK):
 *    - Fires every 0x7270e ticks (~29 seconds at 4us/tick)
 *    - Scans working set lists and triggers page replacement
 *    - Timer flags: 0x1a
 *
 * 2. Update timer (PMAP_$UPDATE_CALLBACK):
 *    - Fires every 0xe5 ticks (~0.9ms at 4us/tick)
 *    - Calls AST_$UPDATE to process deferred page state changes
 *    - Timer flags: 0x16
 *
 * Both timers are configured to first fire at TIME_$CLOCKH + 0xe5 ticks
 * from initialization time.
 *
 * Data layout at 0xE24D44:
 *   0xE24D44: Update timer queue element (time_queue_elem_t, 0x1A bytes)
 *   0xE24D5E: 6 bytes padding
 *   0xE24D64: Purifier timer queue element (time_queue_elem_t, 0x1A bytes)
 *
 * Original address: 0x00e2f880
 * Size: 216 bytes
 */

#include "pmap/pmap_internal.h"
#include "misc/misc.h"

/* Timer queue element data - two elements stored at fixed addresses.
 * These are labeled DAT_00e24d44 (update) and DAT_00e24d64 (purifier) in Ghidra. */
#if defined(ARCH_M68K)
    #define PMAP_UPDATE_TIMER_ELEM   ((time_queue_elem_t *)0xE24D44)
    #define PMAP_PURIFIER_TIMER_ELEM ((time_queue_elem_t *)0xE24D64)
#else
    /* pmap_update_timer_elem, pmap_purifier_timer_elem: pmap_internal.h */
    #define PMAP_UPDATE_TIMER_ELEM   (&pmap_update_timer_elem)
    #define PMAP_PURIFIER_TIMER_ELEM (&pmap_purifier_timer_elem)
#endif

/* Callback function pointers - declared in pmap.h */
/* void PMAP_$T_PURIF_CALLBACK(void);  - at 0xE143CC */
/* void PMAP_$UPDATE_CALLBACK(void);   - at 0xE143B2 */

/* Purifier timer interval: ~29 seconds */
#define PMAP_PURIFIER_INTERVAL  0x7270E

/* Update timer interval: ~0.9ms */
#define PMAP_UPDATE_INTERVAL    0xE5

/* Timer flags */
#define PMAP_PURIFIER_TIMER_FLAGS  0x1A
#define PMAP_UPDATE_TIMER_FLAGS    0x16

void PMAP_$INIT_TIMERS(void)
{
    uint32_t current_time;
    uint32_t first_fire_time;
    status_$t status;
    uint32_t when_high;
    uint16_t when_low;

    current_time = TIME_$CLOCKH;

    /* Set up the purifier timer element */
    PMAP_PURIFIER_TIMER_ELEM->flags = PMAP_PURIFIER_TIMER_FLAGS;
    PMAP_PURIFIER_TIMER_ELEM->callback = (uint32_t)(uintptr_t)PMAP_$T_PURIF_CALLBACK;

    /* First fire time = current time + 0xE5 */
    when_high = current_time;
    when_low = 0;
    first_fire_time = current_time + PMAP_UPDATE_INTERVAL;

    PMAP_PURIFIER_TIMER_ELEM->expire_high = first_fire_time;
    PMAP_PURIFIER_TIMER_ELEM->expire_low = 0;
    PMAP_PURIFIER_TIMER_ELEM->interval_low = 0;
    PMAP_PURIFIER_TIMER_ELEM->interval_high = PMAP_PURIFIER_INTERVAL;

    TIME_$Q_ENTER_ELEM(&TIME_$RTEQ, (clock_t *)&when_high,
                       PMAP_PURIFIER_TIMER_ELEM, &status);

    if (status != status_$ok) {
        CRASH_SYSTEM(&status);
    }

    /* Set up the update timer element */
    PMAP_UPDATE_TIMER_ELEM->flags = PMAP_UPDATE_TIMER_FLAGS;
    PMAP_UPDATE_TIMER_ELEM->callback = (uint32_t)(uintptr_t)PMAP_$UPDATE_CALLBACK;

    /* Reuse the same first fire time */
    when_high = current_time;
    when_low = 0;

    PMAP_UPDATE_TIMER_ELEM->expire_high = first_fire_time;
    PMAP_UPDATE_TIMER_ELEM->expire_low = 0;
    PMAP_UPDATE_TIMER_ELEM->interval_high = PMAP_UPDATE_INTERVAL;
    PMAP_UPDATE_TIMER_ELEM->interval_low = 0;

    TIME_$Q_ENTER_ELEM(&TIME_$RTEQ, (clock_t *)&when_high,
                       PMAP_UPDATE_TIMER_ELEM, &status);

    if (status != status_$ok) {
        CRASH_SYSTEM(&status);
    }
}
