/*
 * TIME_$INIT - Initialize the time subsystem
 *
 * Initializes all time queues, deferred interrupt elements, and
 * optionally reads the calendar from hardware.
 *
 * Parameters:
 *   flags - Pointer to flags byte. If bit 7 is set, read calendar.
 *
 * Original address: 0x00e2fe6c
 *
 * Assembly shows initialization of:
 *   - TIME_$Q_INIT
 *   - TIME_$RTEQ (RTE queue at base+0x1608)
 *   - 64 VT queues (at base+0xC+i*12 + 0x12FC each)
 *   - TIME_$DI_VT and TIME_$DI_RTE
 *   - If flag bit 7 set: read calendar and set clock values
 *   - TIME_$CURRENT_TICK = 0x1047
 *   - TIMER_$INIT
 */

#include "time/time_internal.h"

/*
 * Apollo epoch offset
 *
 * TIME_$CURRENT_TIME is stored as Unix time + this offset.
 * 0x12CEA600 = 315,532,800 = seconds from 1970-01-01 to 1980-01-01
 * (Apollo's epoch was January 1, 1980)
 */
#define APOLLO_EPOCH_OFFSET 0x12CEA600

/*
 * Base address of VT queue array
 *
 * Original: 0xE29198 + 0xC = 0xE291A4
 */
#define VT_QUEUE_ARRAY_BASE 0xE291A4

/*
 * Offset from queue array to actual queue structure
 */
#define VT_QUEUE_OFFSET 0x12FC

void TIME_$INIT(uint8_t *flags)
{
    uint8_t init_flags;
    int i;
    clock_t cal_clock;
    uint32_t cal_time;
    time_queue_t *vt_queue_ptr;

    init_flags = *flags;

    /* Initialize queue subsystem */
    TIME_$Q_INIT();

    /* Initialize the main RTE queue */
    TIME_$Q_INIT_QUEUE(0, 0, &TIME_$RTEQ);

    /*
     * Initialize 64 VT (virtual timer) queues
     * Each queue is 12 bytes apart in the queue table
     * Queue flags = 0xFF (all-queues marker), ID = 1..64
     */
    vt_queue_ptr = (time_queue_t *)VT_QUEUE_ARRAY_BASE;
    for (i = 1; i <= 64; i++) {
        TIME_$Q_INIT_QUEUE(0xFF, (uint16_t)i,
                          (time_queue_t *)((char *)vt_queue_ptr + VT_QUEUE_OFFSET));
        vt_queue_ptr = (time_queue_t *)((char *)vt_queue_ptr + 12);
    }

    /* Initialize deferred interrupt elements */
    DI_$INIT_Q_ELEM(&TIME_$DI_VT);
    DI_$INIT_Q_ELEM(&TIME_$DI_RTE);

    /* If bit 7 set, read calendar and initialize clocks */
    if ((int8_t)init_flags < 0) {  /* Check sign bit (bit 7) */
        TIME_$READ_CAL(&cal_clock, &cal_time);

        /* Set all clock values from calendar */
        TIME_$CLOCKH = cal_clock.high;
        TIME_$CLOCKL = cal_clock.low;
        TIME_$CURRENT_CLOCKH = cal_clock.high;
        TIME_$CURRENT_CLOCKL = cal_clock.low;
        TIME_$BOOT_TIME = cal_clock.high;

        /* Set time of day (add Apollo epoch offset) */
        TIME_$CURRENT_TIME = cal_time + APOLLO_EPOCH_OFFSET;
        TIME_$CURRENT_USEC = 0;

        /* Clear adjustment values */
        TIME_$CURRENT_SKEW = 0;
        TIME_$CURRENT_DELTA = 0;
    }

    /* Set initial tick value */
    TIME_$CURRENT_TICK = TIME_INITIAL_TICK;

    /* Initialize hardware timer */
    TIMER_$INIT();
}
