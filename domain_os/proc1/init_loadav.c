/*
 * PROC1_$INIT_LOADAV - Initialize load average tracking
 * Original address: 0x00e14c94
 *
 * Initializes the load average variables and schedules the periodic
 * callback to update them.
 */

#include "proc1.h"

/*
 * Load average callback structure at 0xe254f4
 * Part of the timer callback entry structure.
 */
typedef struct loadav_callback_entry_t {
    uint32_t field_00;          /* 0x00 (offset from base 0xe254e8) = loadav_1min */
    uint32_t field_04;          /* 0x04 = loadav_5min */
    uint32_t field_08;          /* 0x08 = loadav_15min */
    uint32_t field_0c;          /* 0x0C */
    void     *callback_info;    /* 0x10 = 0x14 from base */
    void     (*callback)(void); /* 0x14 = 0x18 from base */
    uint32_t callback_param;    /* 0x18 = 0x1c from base */
    uint32_t target_time_high;  /* 0x1c = 0x20 from base */
    uint16_t target_time_low;   /* 0x20 = 0x24 from base */
    uint16_t field_22;          /* 0x22 = 0x26 from base - interval ID? */
    uint32_t interval_high;     /* 0x24 = 0x28 from base */
    uint16_t interval_low;      /* 0x28 = 0x2a from base */
} loadav_callback_entry_t;

/* External declarations */
extern void TIME_$CLOCK(void *clock_out);
extern void TIME_$Q_ENTER_ELEM(void *queue, void *time, void *callback_info, status_$t *status);
extern void PROC1_$LOADAV_CALLBACK(void);
extern void *TIME_$RTEQ;  /* Real-time event queue at 0xe2a7a0 */

/*
 * Load average data at 0xe254e8
 */
#if defined(M68K)
    #define LOADAV_BASE         ((loadav_callback_entry_t*)0xe254e8)
    #define LOADAV_1MIN         (*(uint32_t*)0xe254e8)
    #define LOADAV_5MIN         (*(uint32_t*)0xe254ec)
    #define LOADAV_15MIN        (*(uint32_t*)0xe254f0)
#else
    extern loadav_callback_entry_t *LOADAV_BASE;
    extern uint32_t LOADAV_1MIN;
    extern uint32_t LOADAV_5MIN;
    extern uint32_t LOADAV_15MIN;
#endif

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

    /* Clear load averages */
    LOADAV_1MIN = 0;
    LOADAV_5MIN = 0;
    LOADAV_15MIN = 0;

    /* Set interval ID (field at offset 0x22) to 2 */
    LOADAV_BASE->field_22 = 2;

    /* Set up callback function */
    LOADAV_BASE->callback = PROC1_$LOADAV_CALLBACK;
    LOADAV_BASE->callback_param = 0;

    /* Set up interval (5 seconds) */
    delta_high = 0;
    delta_low = 0;
    /* Pack 0x0013:12d0 into the interval fields */
    ((uint16_t*)&delta_high)[0] = 0;      /* High word */
    ((uint16_t*)&delta_high)[1] = LOADAV_INTERVAL_HIGH;  /* Low word of high */
    delta_low = LOADAV_INTERVAL_LOW;

    LOADAV_BASE->interval_high = delta_high;
    LOADAV_BASE->interval_low = delta_low;

    /* Get current time */
    TIME_$CLOCK(&current_time_high);

    /* Add interval to get target time */
    ADD48(&delta_high, &current_time_high);

    /* Store target time */
    LOADAV_BASE->target_time_high = delta_high;
    LOADAV_BASE->target_time_low = delta_low;

    /* Schedule the callback on the real-time event queue */
    TIME_$Q_ENTER_ELEM(&TIME_$RTEQ, &current_time_high, &LOADAV_BASE->callback_info, &status);
}
