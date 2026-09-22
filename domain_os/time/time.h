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
#include "ec/ec.h"

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
 * Timer register access.
 *
 * The three clock readers in the TIME_ASM segment (TIME_$CLOCK 0x00E2AFD6,
 * TIME_$ABS_CLOCK 0x00E2B026, TIME_$GET_TIME_OF_DAY 0x00E2B06A) all do
 *   lea (0xffac00).l,A0 / movep.w (0x5,A0),D0w / btst.b #0,(0x3,A0)
 * i.e. a byte read of 0xFFAC05 and 0xFFAC07 (movep assembles the word from
 * the two odd bytes) and a byte read of 0xFFAC03.  On m68k the macros are the
 * volatile MMIO accesses; on a host build the test supplies
 * time_$timer_read_reg()/time_$timer_write_reg() so the real functions can be
 * driven against a modelled timer (same shape as CAL_$RTC_READ in cal/cal.h).
 */
#if defined(ARCH_M68K)
#define TIME_$TIMER_READ(off) \
    (*(volatile uint8_t *)(uintptr_t)(TIME_TIMER_BASE + (off)))
#define TIME_$TIMER_WRITE(off, val) \
    (*(volatile uint8_t *)(uintptr_t)(TIME_TIMER_BASE + (off)) = (uint8_t)(val))
#else
uint8_t time_$timer_read_reg(uint16_t offset);
void time_$timer_write_reg(uint16_t offset, uint8_t value);
#define TIME_$TIMER_READ(off) time_$timer_read_reg((uint16_t)(off))
#define TIME_$TIMER_WRITE(off, val) \
    time_$timer_write_reg((uint16_t)(off), (uint8_t)(val))
#endif

/* `movep.w (0x5,A0),D0w`: high byte from +5, low byte from +7 */
#define TIME_$READ_RTE_TIMER() \
    ((uint16_t)(((uint16_t)TIME_$TIMER_READ(TIME_TIMER_RTE_HI) << 8) | \
                (uint16_t)TIME_$TIMER_READ(TIME_TIMER_RTE_LO)))

/*
 * TIME status codes.  Names from the SR10.4 status-code database,
 * "OS / time manager" (subsystem 0x0D).
 */
#define status_$time_no_timer_queue_entry           0x000D0001
#define status_$time_entry_to_cancel_not_found      0x000D0002
#define status_$time_quit_while_waiting             0x000D0003
#define status_$time_bad_timer_interrupt            0x000D0004
#define status_$time_bad_timer_key                  0x000D0005
#define status_$time_alarm_fault                    0x000D0006
#define status_$time_real_interval_timer_fault      0x000D0007
#define status_$time_virtual_interval_timer_fault   0x000D0008
#define status_$time_queue_element_not_in_use       0x000D0009
#define status_$time_queue_element_not_found        0x000D000A
#define status_$time_cpu_time_limit_exceeded        0x000D000B
#define status_$time_adjustment_out_of_range        0x000D000C
#define status_$time_queue_element_already_in_use   0x000D000D
#define status_$time_relative_time_is_too_large     0x000D000E

/*
 * ============================================================================
 * Time Queue Structures
 * ============================================================================
 */

/*
 * time_$callback_arg_t - what a queue element's callback is handed
 *
 * The callback's single argument is the ADDRESS of a longword cell; what the
 * cell holds depends on which of TIME_$Q_SCAN_QUEUE's two paths fires it:
 *
 *   - direct (flags bits 2 and 3 both clear, 0x00E16FA0..0x00E16FAE): the
 *     cell is a frame local holding the ELEMENT address (`move.l D2,(-0x8,A6)
 *     / pea (-0x8,A6) / jsr (A0)`), so *arg is the time_queue_elem_t and
 *     TIME_$ADVANCE_CALLBACK reads its callback_arg at (0x8,A1);
 *   - deferred (bit 2 -> DXM_$UNWIRED_Q, bit 3 -> DXM_$WIRED_Q,
 *     0x00E16F42..0x00E16F84): DXM_$ADD_CALLBACK copies the 4 bytes at
 *     &elem->callback_arg into the queue entry and later calls the callback
 *     with the entry's data address, so *arg is the CALLBACK_ARG value (the
 *     itimer / cpu-limit callbacks read an as_id at (0x2,A2) through it).
 *
 * SIO_$I_TSTART's direct-restart path builds the first shape by hand
 * (0x00E1C8F6..0x00E1C904).
 */
typedef uint32_t **time_$callback_arg_t;

/*
 * Time queue header structure - 12 bytes
 *
 * Used for both the RTE queue and VT queues.  TIME_$Q_INIT_QUEUE (0x00E16C5E)
 * clears (A0) and (0x4,A0) and stores the byte at (0x8,A0) and the word at
 * (0xA,A0); every other TIME_Q_ routine passes `pea (0x4,An)` to
 * ML_$SPIN_LOCK / ML_$SPIN_UNLOCK, so +4 is the queue's spin lock, not a
 * tail pointer (the list is singly linked through time_queue_elem_t.next).
 */
typedef struct time_queue_t {
    uint32_t head;          /* 0x00: First element (32-bit VA, 0 = empty) */
    uint32_t lock;          /* 0x04: ML_$SPIN_LOCK cell (ml_$spinlock_t) */
    int8_t   flags;         /* 0x08: Domain boolean: true (0xFF) = virtual
                             *       timer queue, false = the real-time queue
                             *       (tst.b (0x8,A2) / bpl at 0x00E16C0E) */
    uint8_t  pad;           /* 0x09: Padding */
    uint16_t queue_id;      /* 0x0A: Queue identifier (the PID for a VT queue,
                             *       handed to PROC1_$SET_VT at 0x00E16C1E) */
} time_queue_t;

/* Layout recovered from the disassembly -- see the field comments above. */
_Static_assert(__builtin_offsetof(time_queue_t, head) == 0x00, "time_queue_t.head");
_Static_assert(__builtin_offsetof(time_queue_t, lock) == 0x04, "time_queue_t.lock");
_Static_assert(__builtin_offsetof(time_queue_t, flags) == 0x08, "time_queue_t.flags");
_Static_assert(__builtin_offsetof(time_queue_t, pad) == 0x09, "time_queue_t.pad");
_Static_assert(__builtin_offsetof(time_queue_t, queue_id) == 0x0A, "time_queue_t.queue_id");
_Static_assert(sizeof(time_queue_t) == 0x0C, "time_queue_t: TIME_$INIT steps 0xC per queue");

/*
 * time_queue_elem_t.flags bits.  Every test is `btst.b #n,(0x13,Ax)` - byte
 * 0x13 is the LOW byte of the big-endian word at 0x12, so bit n of that byte
 * is bit n of the word.
 */
#define TIME_QELEM_IN_QUEUE   0x0001  /* set by insert (0x00E16B4A), cleared by
                                       * remove (0x00E16BC8) and scan (0x00E16EE4) */
#define TIME_QELEM_REPEAT     0x0002  /* scan re-inserts at expiry + interval
                                       * (0x00E16EEA) */
#define TIME_QELEM_UNWIRED    0x0004  /* fire through DXM_$UNWIRED_Q (0x00E16F3A) */
#define TIME_QELEM_WIRED      0x0008  /* fire through DXM_$WIRED_Q (0x00E16F20) */
#define TIME_QELEM_CHECK_DUP  0x0010  /* DXM_$ADD_CALLBACK's check_dup (0x00E16F0C) */

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

/* Layout recovered from the disassembly -- see the field comments above. */
_Static_assert(__builtin_offsetof(time_queue_elem_t, next) == 0x00, "time_queue_elem_t.next");
_Static_assert(__builtin_offsetof(time_queue_elem_t, callback) == 0x04, "time_queue_elem_t.callback");
_Static_assert(__builtin_offsetof(time_queue_elem_t, callback_arg) == 0x08, "time_queue_elem_t.callback_arg");
_Static_assert(__builtin_offsetof(time_queue_elem_t, expire_high) == 0x0C, "time_queue_elem_t.expire_high");
_Static_assert(__builtin_offsetof(time_queue_elem_t, expire_low) == 0x10, "time_queue_elem_t.expire_low");
_Static_assert(__builtin_offsetof(time_queue_elem_t, flags) == 0x12, "time_queue_elem_t.flags");
_Static_assert(__builtin_offsetof(time_queue_elem_t, interval_high) == 0x14, "time_queue_elem_t.interval_high");
_Static_assert(__builtin_offsetof(time_queue_elem_t, interval_low) == 0x18, "time_queue_elem_t.interval_low");

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
 * Virtual-timer event queues, one per process, Pascal 1-based.
 *
 * TIME_$VTQ: 0xE2A4A0.  Every reference in the image reaches element
 * (PROC1_$CURRENT - 1):
 *   TIME_$VT_INT       0xE163EA/0xE16412/0xE16416  0xE29198 + cur*12 + 0x12FC
 *   TIME_$INIT         loop base 0xE29198 + 0xC, + 0x12FC per iteration
 *   TIME_$RELEASE      0xE58BBE/0xE58BCC  pea (-0xc,A2,D1w) with A2 = 0xE2A4A0
 *   TIME_$SET_CPU_LIMIT 0xE58F88/0xE58F96 lea (-0xc,A0,D0w) with A0 = 0xE2A4A0
 * 0xE29198 + 0x12FC + 0xC == 0xE2A4A0, so all four forms are the same array.
 *
 * The extent is fixed by the next global: 0xE2A7A0 (TIME_$RTEQ) - 0xE2A4A0 =
 * 0x300 = 64 * sizeof(time_queue_t), and TIME_$INIT initialises exactly 64
 * queues with ids 1..64.
 */
#define TIME_MAX_PROCESSES 64
extern time_queue_t TIME_$VTQ[TIME_MAX_PROCESSES];

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
 * TIME_$ADVANCE - Schedule an eventcount advance on the real-time queue
 *
 * Frame at 0x00E16488..0x00E1646C: 0x08 is_absolute (word, by reference),
 * 0x0C when, 0x10 ec (stored as the element's callback_arg and advanced by
 * TIME_$ADVANCE_CALLBACK), 0x14 elem, 0x18 status.
 *
 * Original address: 0x00e16454
 */
void TIME_$ADVANCE(uint16_t *is_absolute, clock_t *when, ec_$eventcount_t *ec,
                   time_queue_elem_t *elem, status_$t *status);

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
/*
 * The first argument is a by-value longword (0x00E164DC reads it with
 * `move.l (0x8,A6),-(SP)` and passes it as the wait value to EC_$WAIT), not a
 * pointer.  Callers push it with `pea (0x1).w`.
 */
void TIME_$CANCEL(int32_t wait_value, time_queue_elem_t *elem,
                  status_$t *status);

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
 * @return: Domain boolean, TRUE (0xFF) when extra_ec was the eventcount
 *          satisfied (EC_$WAIT index 0, `seq` at 0x00E166FE), FALSE (0)
 *          when the timer expired
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
/*
 * TIME_$WRT_VT_TIMER has no C signature: the value arrives in D0w, A0 is
 * preserved and A2 clobbered.  It lives in time/sau2/wrt_vt_timer.s and is
 * only called from PROC1's assembly (0x00E20A64).
 */

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
 * Frame at 0x00E16C62: `move.b (0x8,A6),D0b` reads the HIGH byte of the
 * first argument's word slot (a Domain boolean pushed with `st -(SP)` /
 * `clr.l -(SP)` by TIME_$INIT), then the word at 0x0A and the pointer at
 * 0x0C.
 *
 * Original address: 0x00e16c5e
 */
void TIME_$Q_INIT_QUEUE(boolean is_vt, uint16_t queue_id, time_queue_t *queue);

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
 * Prologue at 0x00e16dd4 fixes the argument shape:
 *   0x08 queue, 0x0C when, 0x10 is_absolute (word), 0x12 now,
 *   0x16 callback, 0x1A callback_arg, 0x1E flags (word), 0x20 interval,
 *   0x24 qelem, 0x28 status.
 * `when` supplies the element's expiry (0xE16DF8); when `is_absolute` is 0
 * the expiry has `*now` added to it (0xE16E0C).  `now` - NOT `when` - is also
 * what is handed to TIME_$Q_ENTER_ELEM (0xE16E34).
 *
 * Original address: 0x00e16dd4
 */
void TIME_$Q_ADD_CALLBACK(time_queue_t *queue, clock_t *when,
                          uint16_t is_absolute, clock_t *now,
                          void *callback, void *callback_arg,
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
 * Frame at 0x00E16E9C: 0x08 queue (A3), 0x0C now (D3), 0x10 status (A4).
 * The third argument is only ever handed on as DXM_$ADD_CALLBACK's
 * status_ret (`pea (A4)` at 0x00E16F42 / 0x00E16F64); the direct-call path
 * never writes it.
 *
 * Original address: 0x00e16e94
 */
void TIME_$Q_SCAN_QUEUE(time_queue_t *queue, clock_t *now, status_$t *status);

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
 * The interval-timer entry points exchange 48-bit values in the SAME 6-byte
 * {high:32, low:16} record as clock_t, but counting in units of two clock
 * ticks: for `*which == 1` the kernel halves each incoming value
 * (time_$itimer_to_clock) and doubles each returned one
 * (time_$clock_to_itimer).  For `*which == 0` no scaling happens at all and
 * the buffers hold plain clock_t values.
 */

/*
 * The two buffer arguments are in Unix `struct itimerval` order: the RELOAD
 * INTERVAL first, the VALUE (time until the next expiry) second.  This is not
 * a guess - the roles are pinned inside time_$set_itimer_internal
 * (0x00E58D14):
 *
 *   - the second argument (0x0A) is handed to TIME_$Q_ADD_CALLBACK as its
 *     `interval` (0x00E58E28 pushes it eighth) and is read back from the
 *     queue element's interval field at +0x14 by time_$get_itimer_internal
 *     (0x00E58CB4), so it is `it_interval`;
 *   - the third argument (0x0E) is the `when` (0x00E58E44 pushes it second),
 *     is the one whose being zero DISARMS the timer (0x00E58DD8 tst.l (A3)),
 *     and is what get returns after subtracting the current clock
 *     (0x00E58CEA SUB48), so it is `it_value`.
 */

/*
 * TIME_$SET_ITIMER - Set interval timer
 *
 * @param which: Pointer to timer type (0 = real, 1 = virtual)
 * @param interval: Pointer to the new reload interval (it_interval)
 * @param value: Pointer to the new time-to-expiry (it_value); zero disarms
 * @param ointerval: Pointer to receive the old reload interval
 * @param ovalue: Pointer to receive the old time-to-expiry
 * @param status: Status return
 *
 * Original address: 0x00e58e58
 */
void TIME_$SET_ITIMER(uint16_t *which, clock_t *interval, clock_t *value,
                      clock_t *ointerval, clock_t *ovalue, status_$t *status);

/*
 * TIME_$GET_ITIMER - Get interval timer
 *
 * @param which: Pointer to timer type (0 = real, 1 = virtual)
 * @param interval: Pointer to receive the reload interval (it_interval)
 * @param value: Pointer to receive the remaining time (it_value)
 *
 * Original address: 0x00e58f06
 */
void TIME_$GET_ITIMER(uint16_t *which, clock_t *interval, clock_t *value);

/*
 * TIME_$SET_CPU_LIMIT - Set CPU time limit
 *
 * @param limit: Pointer to the limit in ITIMER form (0x00E58FA0 converts it
 *               with time_$itimer_to_clock before use)
 * @param relative: Pointer to a Domain boolean: true (0xFF) = relative to the
 *               process's current CPU time, false = absolute
 * @param status: Status return
 *
 * Original address: 0x00e58f64
 */
void TIME_$SET_CPU_LIMIT(clock_t *limit, boolean *relative, status_$t *status);

/*
 * TIME_$RELEASE - Release timer resources for current process
 *
 * Called during process cleanup to release interval timer resources.
 *
 * Original address: 0x00e58b58
 */
void TIME_$RELEASE(void);

#endif /* TIME_H */
