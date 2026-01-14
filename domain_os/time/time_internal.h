/*
 * time_internal.h - TIME Module Internal Definitions
 *
 * This header is for use ONLY within the time/ subsystem.
 * It includes the public time.h and adds internal helpers.
 */

#ifndef TIME_INTERNAL_H
#define TIME_INTERNAL_H

#include "time/time.h"

/* Dependencies on other subsystems */
#include "ml/ml.h"
#include "ec/ec.h"
#include "cal/cal.h"
#include "di/di.h"
#include "proc1/proc1.h"
#include "proc2/proc2.h"
#include "timer/timer.h"
#include "fim/fim.h"

/*
 * ============================================================================
 * Internal Constants
 * ============================================================================
 */

/* itimer database base - 28 bytes per AS */
#define ITIMER_DB_BASE          0xE297F0
#define ITIMER_DB_ENTRY_SIZE    0x1C  /* 28 bytes */

/* Offsets within itimer entry */
#define ITIMER_REAL_INTERVAL_HIGH   0x0C
#define ITIMER_REAL_INTERVAL_LOW    0x10
#define ITIMER_VIRT_INTERVAL_HIGH   0x664
#define ITIMER_VIRT_INTERVAL_LOW    0x668

/* CPU limit database base */
#define CPU_LIMIT_DB_BASE       0xE29198
#define CPU_LIMIT_DB_ENTRY_SIZE 0x1C

/* VT queue array base */
#define VT_QUEUE_ARRAY_BASE     0xE29198
#define VT_QUEUE_OFFSET         0x12FC

/* Signal numbers (also in proc2/proc2.h but needed here) */
#define TIME_SIGALRM     14
#define TIME_SIGVTALRM   26
#define TIME_SIGXCPU     24

/* Apollo epoch offset (seconds from 1970 to 1980) */
#define APOLLO_EPOCH_OFFSET     0x12CEA600

/* Maximum adjustment allowed (seconds) */
#define MAX_ADJUST_SECONDS      8000

/* Ticks per second */
#define TICKS_PER_SECOND        250000

/* Skew divisors */
#define SKEW_DIVISOR_SLOW       0x00A7
#define SKEW_DIVISOR_FAST       0x0686

/*
 * ============================================================================
 * Internal Data
 * ============================================================================
 */

/* Fast clock event count for TIME_$GET_EC */
extern uint32_t TIME_$FAST_CLOCK_EC;

/*
 * ============================================================================
 * Internal Function Prototypes
 * ============================================================================
 */

/*
 * time_$q_insert_sorted - Insert element into queue in sorted order
 *
 * Returns negative if element was inserted at head.
 */
int8_t time_$q_insert_sorted(time_queue_t *queue, time_queue_elem_t *elem);

/*
 * time_$q_setup_timer - Setup hardware timer for next queue element
 */
void time_$q_setup_timer(time_queue_t *queue, clock_t *when);

/*
 * time_$q_remove_internal - Internal queue removal (no locking)
 */
void time_$q_remove_internal(time_queue_t *queue, time_queue_elem_t *elem,
                             status_$t *status);

/*
 * time_$itimer_to_clock - Convert itimerval to clock_t
 */
void time_$itimer_to_clock(clock_t *clock, uint32_t *itimerval);

/*
 * time_$clock_to_itimer - Convert clock_t to itimerval
 */
void time_$clock_to_itimer(clock_t *clock, uint32_t *itimerval);

/*
 * time_$get_itimer_internal - Get raw itimer values
 */
void time_$get_itimer_internal(uint16_t which, clock_t *value, clock_t *interval);

/*
 * time_$set_itimer_internal - Set itimer with clock_t values
 */
void time_$set_itimer_internal(uint16_t which, clock_t *value, clock_t *interval,
                               clock_t *ovalue, clock_t *ointerval,
                               status_$t *status);

/*
 * TIME_$TIMER_HANDLER - Hardware timer interrupt entry point
 */
void TIME_$TIMER_HANDLER(void);

/*
 * TIME_$SET_ITIMER_REAL_CALLBACK - Callback for real interval timer
 * Original address: 0x00e58c2c
 */
void TIME_$SET_ITIMER_REAL_CALLBACK(void *arg);

/*
 * TIME_$SET_ITIMER_VIRT_CALLBACK - Callback for virtual interval timer
 * Original address: 0x00e58d14
 */
void TIME_$SET_ITIMER_VIRT_CALLBACK(void *arg);

/*
 * TIME_$SET_CPU_LIMIT_CALLBACK - Callback for CPU limit timer
 * Original address: 0x00e58dfc
 */
void TIME_$SET_CPU_LIMIT_CALLBACK(void *arg);

#endif /* TIME_INTERNAL_H */
