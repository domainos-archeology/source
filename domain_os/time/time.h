/*
 * TIME - Time Management Module
 *
 * Provides system clock, timers, and time-based event queues.
 *
 * The system uses two timers:
 *   - Real-time timer (RTE): Absolute time events
 *   - Virtual timer (VT): Process virtual time events
 *
 * Clock values are 48-bit (32-bit high + 16-bit low) representing
 * 4-microsecond ticks (250,000 ticks per second).
 *
 * Hardware timer registers at 0xFFAC00:
 *   - 0xFFAC03: Control/status byte
 *   - 0xFFAC05,07: Real-time timer counter (movep.w access)
 *   - 0xFFAC09,0B: Virtual timer counter (movep.w access)
 */

#ifndef TIME_H
#define TIME_H

#include "base/base.h"
#include "di/di.h"

/*
 * ============================================================================
 * Hardware Timer Definitions
 * ============================================================================
 */

/* Hardware timer register base address */
#define TIME_TIMER_BASE     0xFFAC00

/* Timer control/status register offsets */
#define TIME_TIMER_CTRL     0x03    /* Control/status byte */
#define TIME_TIMER_RTE_HI   0x05    /* Real-time timer counter high byte */
#define TIME_TIMER_RTE_LO   0x07    /* Real-time timer counter low byte */
#define TIME_TIMER_VT_HI    0x09    /* Virtual timer counter high byte */
#define TIME_TIMER_VT_LO    0x0B    /* Virtual timer counter low byte */

/* Timer control bits */
#define TIME_CTRL_RTE_INT   0x01    /* Real-time timer interrupt pending */
#define TIME_CTRL_VT_INT    0x02    /* Virtual timer interrupt pending */

/* Timer constant: initial tick value */
#define TIME_INITIAL_TICK   0x1047

/*
 * ============================================================================
 * Time Queue Structures
 * ============================================================================
 */

/*
 * Time queue header structure - 12 bytes
 *
 * Used for both the RTE queue and VT queues.
 */
typedef struct time_queue_t {
    uint32_t head;          /* 0x00: First element pointer */
    uint32_t tail;          /* 0x04: Last element pointer */
    uint8_t  flags;         /* 0x08: Queue flags (0xFF = all queues) */
    uint8_t  pad;           /* 0x09: Padding */
    uint16_t queue_id;      /* 0x0A: Queue identifier */
} time_queue_t;

/*
 * Time queue element structure - 26 bytes (0x1A)
 *
 * Used for callback entries in time queues.
 */
typedef struct time_queue_elem_t {
    uint32_t next;          /* 0x00: Next element pointer */
    uint32_t callback;      /* 0x04: Callback function pointer */
    uint32_t callback_arg;  /* 0x08: Callback argument */
    uint32_t expire_high;   /* 0x0C: Expiration time high word */
    uint16_t expire_low;    /* 0x10: Expiration time low word */
    uint16_t flags;         /* 0x12: Element flags */
    uint32_t interval_high; /* 0x14: Repeat interval high word */
    uint16_t interval_low;  /* 0x18: Repeat interval low word */
} time_queue_elem_t;

/*
 * ============================================================================
 * Global Data Declarations
 * ============================================================================
 */

/*
 * Absolute clock values (adjusted for drift/skew)
 *
 * TIME_$CLOCKH: 0xE2B0D4 - High 32 bits
 * TIME_$CLOCKL: 0xE2B0E0 - Low 16 bits
 */
extern uint32_t TIME_$CLOCKH;
extern uint16_t TIME_$CLOCKL;

/*
 * Current clock values (raw, not adjusted)
 *
 * TIME_$CURRENT_CLOCKH: 0xE2B0E4
 * TIME_$CURRENT_CLOCKL: 0xE2B0E8
 */
extern uint32_t TIME_$CURRENT_CLOCKH;
extern uint16_t TIME_$CURRENT_CLOCKL;

/*
 * Boot time (clock value at system start)
 *
 * TIME_$BOOT_TIME: 0xE2B0EC
 */
extern uint32_t TIME_$BOOT_TIME;

/*
 * Current time of day (Unix-style seconds + microseconds)
 *
 * TIME_$CURRENT_TIME: 0xE2B0F0 - Seconds since epoch
 * TIME_$CURRENT_USEC: 0xE2B0F4 - Microseconds within second
 */
extern uint32_t TIME_$CURRENT_TIME;
extern uint32_t TIME_$CURRENT_USEC;

/*
 * Current tick counter
 *
 * TIME_$CURRENT_TICK: 0xE2B0F8
 */
extern uint16_t TIME_$CURRENT_TICK;

/*
 * Clock adjustment values
 *
 * TIME_$CURRENT_SKEW: 0xE2B0FA - Current skew adjustment
 * TIME_$CURRENT_DELTA: 0xE2B0FC - Current delta adjustment
 */
extern uint16_t TIME_$CURRENT_SKEW;
extern uint32_t TIME_$CURRENT_DELTA;

/*
 * Interrupt-in-progress flags
 *
 * IN_VT_INT: 0xE2AF6A
 * IN_RT_INT: 0xE2AF6B
 */
extern uint8_t IN_VT_INT;
extern uint8_t IN_RT_INT;

/*
 * Real-time event queue
 *
 * TIME_$RTEQ: 0xE2A7A0 (base 0xE29198 + offset 0x1608)
 */
extern time_queue_t TIME_$RTEQ;

/*
 * Deferred interrupt queue elements
 *
 * TIME_$DI_VT: 0xE2B10E
 * TIME_$DI_RTE: 0xE2B11E
 */
extern di_queue_elem_t TIME_$DI_VT;
extern di_queue_elem_t TIME_$DI_RTE;

/*
 * ============================================================================
 * Function Prototypes
 * ============================================================================
 */

/*
 * TIME_$INIT - Initialize the time subsystem
 *
 * Parameters:
 *   flags - Initialization flags (bit 7 set = read calendar)
 *
 * Original address: 0x00e2fe6c
 */
void TIME_$INIT(uint8_t *flags);

/*
 * TIME_$CLOCK - Get current clock value (adjusted)
 *
 * Returns the current 48-bit clock value, adjusted for drift.
 *
 * Parameters:
 *   clock - Pointer to receive clock value
 *
 * Original address: 0x00e2afd6
 */
void TIME_$CLOCK(clock_t *clock);

/*
 * TIME_$ABS_CLOCK - Get absolute clock value
 *
 * Returns the absolute 48-bit clock value.
 *
 * Parameters:
 *   clock - Pointer to receive clock value
 *
 * Original address: 0x00e2b026
 */
void TIME_$ABS_CLOCK(clock_t *clock);

/*
 * TIME_$GET_TIME_OF_DAY - Get current time of day
 *
 * Returns seconds and microseconds since epoch.
 *
 * Parameters:
 *   tv - Pointer to receive timeval (seconds, microseconds)
 *
 * Original address: 0x00e2b06a
 */
void TIME_$GET_TIME_OF_DAY(uint32_t *tv);

/*
 * TIME_$SET_TIME_OF_DAY - Set current time of day
 *
 * Original address: 0x00e1678c
 */
void TIME_$SET_TIME_OF_DAY(uint32_t *tv, status_$t *status);

/*
 * TIME_$ADJUST_TIME_OF_DAY - Adjust time of day gradually
 *
 * @param delta: Pointer to delta (seconds, microseconds pair)
 * @param old_delta: Pointer to receive previous delta
 * @param status: Status return
 *
 * Original address: 0x00e168de
 */
void TIME_$ADJUST_TIME_OF_DAY(int32_t *delta, int32_t *old_delta, status_$t *status);

/*
 * TIME_$ADVANCE - Schedule a callback after a delay
 *
 * Parameters:
 *   delay_type - Pointer to delay type
 *   delay - Pointer to delay clock value
 *   ec - Event counter or callback context
 *   callback_arg - Argument for callback
 *   status - Status return
 *
 * Original address: 0x00e16454
 */
void TIME_$ADVANCE(uint16_t *delay_type, clock_t *delay, void *ec,
                   void *callback_arg, status_$t *status);

/*
 * TIME_$CANCEL - Cancel a scheduled callback
 *
 * Parameters:
 *   ec - Event count to signal on completion
 *   elem - Queue element to cancel
 *   status - Status return
 *
 * Original address: 0x00e164a4
 */
void TIME_$CANCEL(uint32_t *ec, void *elem, status_$t *status);

/*
 * TIME_$WAIT - Wait for a specified time
 *
 * @param delay_type: Pointer to delay type (0=relative, 1=absolute)
 * @param delay: Pointer to delay clock value
 * @param status: Status return
 *
 * Original address: 0x00e1650a
 */
void TIME_$WAIT(uint16_t *delay_type, clock_t *delay, status_$t *status);

/*
 * TIME_$WAIT2 - Wait for a specified time with extra event count
 *
 * Like TIME_$WAIT but allows waiting on an additional event count.
 * Returns which EC triggered the wakeup.
 *
 * @param delay_type: Pointer to delay type (0=relative, 1=absolute)
 * @param delay: Pointer to delay clock value
 * @param extra_ec: Additional event count to wait on
 * @param count: Pointer to count value for EC wait
 * @param status: Status return
 * @return: -1 if timer triggered, 0 if extra_ec triggered
 *
 * Original address: 0x00e16654
 */
int8_t TIME_$WAIT2(uint16_t *delay_type, clock_t *delay, void *extra_ec,
                   uint32_t *count, status_$t *status);

/*
 * TIME_$GET_EC - Get time eventcount
 *
 * @param ec_id: Pointer to eventcount ID (0=clock, 1=fast_clock)
 * @param ec_ret: Pointer to receive eventcount pointer
 * @param status: Status return
 *
 * Original address: 0x00e1670a
 */
void TIME_$GET_EC(uint16_t *ec_id, void **ec_ret, status_$t *status);

/*
 * TIME_$GET_ADJUST - Get clock adjustment values
 *
 * @param delta: Pointer to receive adjustment (seconds, microseconds pair)
 *
 * Original address: 0x00e16aa8
 */
void TIME_$GET_ADJUST(int32_t *delta);

/*
 * TIME_$SET_VECTOR - Set timer interrupt vector
 *
 * Sets up the interrupt vector for the time subsystem.
 * Writes the handler address to vector 0x78.
 *
 * Original address: 0x00e2b102
 */
void TIME_$SET_VECTOR(void);

/*
 * TIME_$READ_CAL - Read calendar from hardware
 *
 * Original address: 0x00e2af5e
 */
void TIME_$READ_CAL(clock_t *clock, uint32_t *time);

/*
 * TIME_$VT_TIMER - Read virtual timer
 *
 * Returns current virtual timer value, or 0 if interrupt pending.
 *
 * Original address: 0x00e2af6c
 */
uint16_t TIME_$VT_TIMER(void);

/*
 * TIME_$WRT_VT_TIMER - Write virtual timer
 *
 * Original address: 0x00e2af8a
 */
void TIME_$WRT_VT_TIMER(uint16_t value);

/*
 * TIME_$WRT_TIMER - Write to a hardware timer
 *
 * Writes a value to one of the hardware timers (0-3).
 * Timer indices:
 *   0 - Control registers (0xFFAC01, 0xFFAC03)
 *   1 - Real-time event timer (0xFFAC05, 0xFFAC07)
 *   2 - Virtual timer (0xFFAC09, 0xFFAC0B)
 *   3 - Auxiliary timer (0xFFAC0D, 0xFFAC0F)
 *
 * Also clears interrupt flags:
 *   - Timer 2: clears IN_VT_INT
 *   - Timer 3: clears IN_RT_INT
 *
 * Parameters:
 *   timer_index - Pointer to timer index (0-3)
 *   value       - Pointer to 16-bit value to write
 *
 * Original address: 0x00e2afa0
 */
void TIME_$WRT_TIMER(uint16_t *timer_index, uint16_t *value);

/*
 * ============================================================================
 * Queue Management Functions
 * ============================================================================
 */

/*
 * TIME_$Q_INIT - Initialize queue subsystem
 *
 * Original address: 0x00e16c5c
 */
void TIME_$Q_INIT(void);

/*
 * TIME_$Q_INIT_QUEUE - Initialize a time queue
 *
 * Original address: 0x00e16c5e
 */
void TIME_$Q_INIT_QUEUE(uint8_t flags, uint16_t queue_id, time_queue_t *queue);

/*
 * TIME_$Q_FLUSH_QUEUE - Flush all elements from a queue
 *
 * Original address: 0x00e16c80
 */
void TIME_$Q_FLUSH_QUEUE(time_queue_t *queue);

/*
 * TIME_$Q_REENTER_ELEM - Re-enter an element into queue
 *
 * Original address: 0x00e16c8e
 *
 * Parameters:
 *   queue - The queue to enter into
 *   when - Target time for callback
 *   flags - Flags (0 typically)
 *   base_time - Base time reference
 *   elem - Queue element with callback info
 *   status - Status return
 */
void TIME_$Q_REENTER_ELEM(time_queue_t *queue, clock_t *when, int16_t qflags,
                          clock_t *base_time, time_queue_elem_t *elem,
                          status_$t *status);

/*
 * TIME_$Q_ENTER_ELEM - Enter an element into queue
 *
 * Original address: 0x00e16d64
 */
void TIME_$Q_ENTER_ELEM(time_queue_t *queue, clock_t *when,
                        time_queue_elem_t *elem, status_$t *status);

/*
 * TIME_$Q_ADD_CALLBACK - Add a callback to the queue
 *
 * Original address: 0x00e16dd4
 */
void TIME_$Q_ADD_CALLBACK(time_queue_t *queue, void *elem, uint16_t relative,
                          clock_t *when, void *callback, void *callback_arg,
                          uint16_t flags, clock_t *interval,
                          time_queue_elem_t *qelem, status_$t *status);

/*
 * TIME_$Q_REMOVE_ELEM - Remove an element from queue
 *
 * Original address: 0x00e16e48
 */
void TIME_$Q_REMOVE_ELEM(time_queue_t *queue, time_queue_elem_t *elem,
                         status_$t *status);

/*
 * TIME_$Q_SCAN_QUEUE - Scan queue and fire expired callbacks
 *
 * Original address: 0x00e16e94
 */
void TIME_$Q_SCAN_QUEUE(time_queue_t *queue, clock_t *now, void *arg);

/*
 * ============================================================================
 * Interrupt Handlers
 * ============================================================================
 */

/*
 * TIME_$RTE_INT - Real-time timer interrupt handler
 *
 * Original address: 0x00e163a6
 */
void TIME_$RTE_INT(void);

/*
 * TIME_$VT_INT - Virtual timer interrupt handler
 *
 * Original address: 0x00e163e4
 */
void TIME_$VT_INT(void);

/*
 * TIME_$ADVANCE_CALLBACK - Internal callback for TIME_$ADVANCE
 *
 * Original address: 0x00e16434
 */
void TIME_$ADVANCE_CALLBACK(void *arg);

/*
 * ============================================================================
 * Interval Timer Functions (for Unix compatibility)
 * ============================================================================
 */

/*
 * TIME_$SET_ITIMER - Set interval timer
 *
 * @param which: Pointer to timer type (0=ITIMER_REAL, 1=ITIMER_VIRTUAL, 2=ITIMER_PROF)
 * @param value: Pointer to new value (clock_t format)
 * @param interval: Pointer to new interval (clock_t format)
 * @param ovalue: Pointer to receive old value (clock_t format)
 * @param ointerval: Pointer to receive old interval (clock_t format)
 * @param status: Status return
 *
 * Original address: 0x00e58e58
 */
void TIME_$SET_ITIMER(uint16_t *which, uint32_t *value, uint32_t *interval,
                      uint32_t *ovalue, uint32_t *ointerval, status_$t *status);

/*
 * TIME_$GET_ITIMER - Get interval timer
 *
 * @param which: Pointer to timer ID
 * @param value: Pointer to receive current value
 * @param interval: Pointer to receive interval
 *
 * Original address: 0x00e58f06
 */
void TIME_$GET_ITIMER(uint16_t *which, uint32_t *value, uint32_t *interval);

/*
 * TIME_$SET_CPU_LIMIT - Set CPU time limit
 *
 * @param limit: Pointer to limit value (clock_t)
 * @param relative: Pointer to flag: 0 = absolute, non-zero = relative to current CPU time
 * @param status: Status return
 *
 * Original address: 0x00e58f64
 */
void TIME_$SET_CPU_LIMIT(clock_t *limit, int8_t *relative, status_$t *status);

/*
 * TIME_$RELEASE - Release timer resources for current process
 *
 * Called during process cleanup to release interval timer resources.
 *
 * Original address: 0x00e58b58
 */
void TIME_$RELEASE(void);

#endif /* TIME_H */
