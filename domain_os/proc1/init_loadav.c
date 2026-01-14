/*
 * PROC1_$INIT_LOADAV - Initialize load average tracking
 * Original address: 0x00e14c94
 *
 * Initializes the load average variables and schedules the periodic
 * callback to update them.
 */

#include "proc1.h"

/* External declarations */
extern void TIME_$CLOCK(void *clock_out);
extern void TIME_$Q_ENTER_ELEM(void *queue, void *time, void *callback_info, status_$t *status);
extern void PROC1_$LOADAV_CALLBACK(void);
extern void *TIME_$RTEQ;  /* Real-time event queue at 0xe2a7a0 */

/*
 * Callback interval: 0x0013:12d0 = 5 seconds in clock ticks
 * (assuming 250 ticks per second: 5 * 250 * 256 = 0x13:12d0 in 48-bit format)
 */
#define LOADAV_INTERVAL_HIGH    0x0013
#define LOADAV_INTERVAL_LOW     0x12d0

void PROC1_$INIT_LOADAV(void)
{
    status_$t status;
    uint32_t current_time_high;
    uint16_t current_time_low;
    uint32_t delta_high;
    uint16_t delta_low;
    ts_timer_entry_t *loadav_entry;

    /*
     * The load average data shares space with timer table entry 0.
     * The first 12 bytes (3 uint32_t) are the load averages,
     * followed by the callback entry structure.
     */
    loadav_entry = &TS_TIMER_TABLE[0];

    /* Clear load averages */
    LOADAV_1MIN = 0;
    LOADAV_5MIN = 0;
    LOADAV_15MIN = 0;

    /* Set interval ID (field_26) to 2 */
    loadav_entry->field_26 = 2;

    /* Set up callback function */
    loadav_entry->callback = (void (*)(void*))PROC1_$LOADAV_CALLBACK;
    loadav_entry->callback_param = 0;

    /* Set up interval (5 seconds) */
    delta_high = 0;
    delta_low = 0;
    /* Pack 0x0013:12d0 into the interval fields */
    ((uint16_t*)&delta_high)[0] = 0;      /* High word */
    ((uint16_t*)&delta_high)[1] = LOADAV_INTERVAL_HIGH;  /* Low word of high */
    delta_low = LOADAV_INTERVAL_LOW;

    /* Store interval in entry (fields not explicitly modeled yet) */
    /* For now just set target time */

    /* Get current time */
    TIME_$CLOCK(&current_time_high);

    /* Add interval to get target time */
    ADD48(&delta_high, &current_time_high);

    /* Store target time */
    loadav_entry->cpu_time_high = delta_high;
    loadav_entry->cpu_time_low = delta_low;

    /* Schedule the callback on the real-time event queue */
    TIME_$Q_ENTER_ELEM(&TIME_$RTEQ, &current_time_high, loadav_entry->callback_info, &status);
}
