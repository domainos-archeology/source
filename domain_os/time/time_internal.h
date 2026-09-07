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

/*
 * Per-address-space timer databases.
 *
 * Three arrays of 0x1C-byte (padded time_queue_elem_t) records sit back to
 * back in the image, all indexed 0-based by PROC1_$AS_ID with the same
 * "id*32 - id*4" sequence (e.g. 0xE58A48..0xE58A58 in
 * TIME_$SET_ITIMER_REAL_CALLBACK):
 *
 *   0xE29198  TIME_$CPU_LIMIT_DB      58 * 0x1C = 0x658
 *   0xE297F0  TIME_$ITIMER_DB, real   58 * 0x1C = 0x658
 *   0xE29E48  TIME_$ITIMER_DB, virt   58 * 0x1C = 0x658   (= real + 0x658)
 *   0xE2A4A0  TIME_$VTQ (see time.h)
 *
 * The virtual half is only ever reached as "real entry + 0x658", which is why
 * the interval fields appear at 0x664/0x668 (= 0x658 + 0x0C/0x10).
 */
#define ITIMER_DB_BASE          0xE297F0
#define ITIMER_DB_ENTRY_SIZE    0x1C  /* 28 bytes */

/* Offsets within itimer entry */
#define ITIMER_REAL_INTERVAL_HIGH   0x0C
#define ITIMER_REAL_INTERVAL_LOW    0x10
#define ITIMER_VIRT_ELEM_OFFSET     0x658
#define ITIMER_VIRT_INTERVAL_HIGH   0x664
#define ITIMER_VIRT_INTERVAL_LOW    0x668

/* CPU limit database base */
#define CPU_LIMIT_DB_BASE       0xE29198
#define CPU_LIMIT_DB_ENTRY_SIZE 0x1C

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
 * Pascal by-reference constant cells at 0x00E58B52 / 0x00E58B54, shared by
 * TIME_$SET_CPU_LIMIT_CALLBACK (pea (0x20,PC) / pea (0x26,PC)) and
 * TIME_$SET_CPU_LIMIT (pea (-0x4f8,PC) / pea (-0x4f2,PC)).
 */
extern const int16_t time_$c_cpu_limit_signal;
extern const status_$t time_$c_cpu_limit_fault;

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
 * time_$itimer_to_clock - 48-bit right shift by one of *src into *dst
 * (0x00E58C3A).  A0 = (0xc,A6) is read, A1 = (0x8,A6) is written, so the
 * DESTINATION is the first argument.  The shift moves bit 0 of the source
 * high longword into bit 15 of the destination low word
 * (btst.b #0,(0x3,A0) / bset.b #7,(-0x8,A6)).
 *
 * The "itimer" form is the same 6-byte {high:32, low:16} record as clock_t,
 * counting in units of two clock ticks.
 */
void time_$itimer_to_clock(clock_t *dst_clock, const clock_t *src_itimer);

/*
 * time_$clock_to_itimer - 48-bit LEFT shift by one of *src into *dst
 * (0x00E58C02).  Same register split: A0 = (0xc,A6) source,
 * A1 = (0x8,A6) destination.  cmpi.w #-0x8000,(0x4,A0) / bcs / addq.l #1
 * carries bit 15 of the source low word into the destination high longword.
 */
void time_$clock_to_itimer(clock_t *dst_itimer, const clock_t *src_clock);

/*
 * time_$get_itimer_internal - Get raw itimer values
 * 0x00E58C74.  TODO(source-gvvn): not decompiled yet.
 */
void time_$get_itimer_internal(uint16_t which, clock_t *value, clock_t *interval);

/*
 * time_$set_itimer_internal - Set itimer with clock_t values
 * 0x00E58D14, six parameters at 0x08 which(w), 0x0A value, 0x0E interval,
 * 0x12 ovalue, 0x16 ointerval, 0x1A status.
 * TODO(source-gvvn): not decompiled yet.
 */
void time_$set_itimer_internal(uint16_t which, clock_t *value, clock_t *interval,
                               clock_t *ovalue, clock_t *ointerval,
                               status_$t *status);

/*
 * TIME_$TIMER_HANDLER - Hardware timer interrupt entry point
 */
void TIME_$TIMER_HANDLER(void);

/*
 * The three timer callbacks below are reached through TIME_$Q_SCAN_QUEUE's
 * deferred path (0x00E16F4C..0x00E16F84): the scanner builds a local holding
 * &elem->callback_arg and passes the ADDRESS of that local, so the callback's
 * single argument is a "uint32_t **" whose target is the callback_arg
 * longword.  All three then read its low word as the address-space id
 * (movea.l (A0),A2 / move.w (0x2,A2),D0w).
 */
typedef uint32_t **time_$callback_arg_t;

/*
 * TIME_$SET_ITIMER_REAL_CALLBACK - Callback for real interval timer
 * Original address: 0x00e58a38
 */
void TIME_$SET_ITIMER_REAL_CALLBACK(time_$callback_arg_t arg);

/*
 * TIME_$SET_ITIMER_VIRT_CALLBACK - Callback for virtual interval timer
 * Original address: 0x00e58a98
 */
void TIME_$SET_ITIMER_VIRT_CALLBACK(time_$callback_arg_t arg);

/*
 * TIME_$SET_CPU_LIMIT_CALLBACK - Callback for CPU limit timer
 * Original address: 0x00e58af8
 */
void TIME_$SET_CPU_LIMIT_CALLBACK(time_$callback_arg_t arg);

#endif /* TIME_INTERNAL_H */
