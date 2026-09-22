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

/*
 * Distance from the real half of TIME_$ITIMER_DB to the virtual half.  Both
 * time_$get_itimer_internal (0x00E58C90 mulu.w #0x658,D1) and
 * time_$set_itimer_internal (0x00E58DB0) form the per-`which` base with a
 * single multiply, so the two halves are one array indexed [which][as_id].
 */
#define ITIMER_DB_WHICH_STRIDE  0x658

/*
 * The entries are addressed through time_$itimer_entry() below and their
 * fields through time_queue_elem_t.  (Earlier ITIMER_*_INTERVAL_* byte
 * offsets 0x0C/0x10 and 0x664/0x668 named the element's EXPIRY words, not
 * its interval at 0x14/0x18 - bead source-e4a2; they are gone.)
 */

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

/*
 * TIME_$FAST_CLOCK_EC - the fast-clock level-1 eventcount (map: symbol
 * TIME_$FAST_CLOCK_EC at 0x00E2B0C8, the next map symbol TIME_$CLOCKH_EC is
 * at 0x00E2B0D4, so the object is exactly 0x0C bytes = one ec_$eventcount_t).
 * The image holds { 0, 0x00E2B0C8, 0x00E2B0C8 }: an EC_$INIT'd empty circular
 * waiter list whose head and tail point at the eventcount itself.
 */
extern ec_$eventcount_t TIME_$FAST_CLOCK_EC;

/*
 * Unnamed cells of the TIME_ data segment (map: D E29198 TIME_ size 1628),
 * between TIME_$RTEQ (0xE2A7A0, 12 bytes) and TIME_$SYS_FREQ (0xE2A7BC):
 *
 *   0xE2A7AC  time_$zero_interval      6 bytes, all zero in the image; the
 *             one-shot interval TIME_$ADVANCE passes by reference
 *             (`pea (0x1614,A5)` at 0x00E16474, A5 = 0xE29198).
 *   0xE2A7B4  time_$fast_clock_ec_handle  (0x161C,A5) - TIME_$GET_EC's
 *             cached EC2_$REGISTER_EC1 result for TIME_$FAST_CLOCK_EC
 *   0xE2A7B8  time_$clock_ec_handle       (0x1620,A5) - the same for
 *             TIME_$CLOCKH_EC (0x00E2B0D4)
 *
 * The handles are 32-bit VAs in the image (EC2_$REGISTER_EC1 returns its
 * result in A0 and TIME_$GET_EC stores it with `move.l A0,(0x1620,A5)`);
 * they are kept as the host pointer type the EC2 routine returns because
 * nothing but TIME_$GET_EC ever looks at them.
 */
extern clock_t time_$zero_interval;
extern void *time_$fast_clock_ec_handle;
extern void *time_$clock_ec_handle;

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
 * time_$q_insert_sorted - Insert element into queue in expiry order
 *
 * 0x00E16AE8, 130 bytes; the first (unnamed) procedure of the TIME_Q_ module.
 * Frame: 0x08 queue (A4), 0x0C elem.  Crashes the system with
 * status_$time_queue_element_already_in_use when the element's
 * TIME_QELEM_IN_QUEUE bit is already set.  Returns a Domain boolean in D0:
 * true (0xFF, `seq D0b` at 0x00E16B54) when the element became the new head.
 */
int8_t time_$q_insert_sorted(time_queue_t *queue, time_queue_elem_t *elem);

/*
 * time_$q_setup_timer - Program the hardware timer for the queue's head
 *
 * 0x00E16BDA, 126 bytes.  Frame: 0x08 queue (A2), 0x0C now.
 */
void time_$q_setup_timer(time_queue_t *queue, clock_t *now);

/*
 * time_$q_remove_internal - Unlink an element (caller holds the queue lock)
 *
 * 0x00E16B70, 106 bytes.  Frame: 0x08 queue (A2), 0x0C elem, 0x10 status.
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
 * time_$itimer_entry - address of TIME_$ITIMER_DB[which][as_id]
 *
 * Both internal entry points build the address the same way, with two
 * word-scaled `lea`s off a longword base:
 *
 *   00e58c8a  movea.l #0xe297f0,A1
 *   00e58c90  mulu.w #0x658,D1              ; D1 = which * 0x658
 *   00e58c94  lea (0x0,A1,D1w*0x1),A1
 *   00e58c98  move.w (0x00e2060a).l,D1w     ; PROC1_$AS_ID
 *   00e58c9e  lsl.w #0x2,D1w                ; D1 = id*4
 *   00e58ca0  move.w D1w,D2w
 *   00e58ca2  neg.w D1w                     ; D1 = -id*4
 *   00e58ca4  lsl.w #0x3,D2w                ; D2 = id*32
 *   00e58ca6  add.w D2w,D1w                 ; D1 = id*28 = id*0x1C
 *   00e58ca8  lea (0x0,A1,D1w*0x1),A1
 *
 * Both index registers are used as SIGN-EXTENDED WORDS, which is why the two
 * products are truncated to int16_t here.  (0x00E58DA8..0x00E58DC8 in
 * time_$set_itimer_internal is the same sequence with A4/D0.)
 *
 * The entries are 0x1C bytes: a 0x1A-byte time_queue_elem_t plus two bytes of
 * padding.
 */
static inline time_queue_elem_t *time_$itimer_entry(uint16_t which,
                                                    uint16_t as_id)
{
    int16_t which_offset = (int16_t)(which * ITIMER_DB_WHICH_STRIDE);
    int16_t as_offset = (int16_t)(as_id * ITIMER_DB_ENTRY_SIZE);

    return (time_queue_elem_t *)ARCH_VA_TO_PTR(ITIMER_DB_BASE +
                                               which_offset + as_offset);
}

/*
 * time_$itimer_active - the queue element's "in use" bit
 *
 * 0x00E58CAC "btst.b #0x0,(0x13,A1)": byte 0x13 is the LOW byte of the
 * time_queue_elem_t.flags word at 0x12 (big-endian), so bit 0 of that byte is
 * bit 0 of the word.
 */
#define ITIMER_FLAG_IN_USE      0x0001

/*
 * The flags word time_$set_itimer_internal hands TIME_$Q_ADD_CALLBACK:
 * "moveq #0x4,D0 / or.w D2w,D0w" at 0x00E58E2A, where D2 is 0x12 when the
 * reload interval is non-zero (0x00E58E0C) and 0 otherwise (0x00E58E10).
 */
#define ITIMER_FLAG_BASE        0x0004
#define ITIMER_FLAG_REPEATING   0x0012

/*
 * time_$get_itimer_internal - read TIME_$ITIMER_DB[which][PROC1_$AS_ID]
 *
 * 0x00E58C74, 160 bytes.  Three parameters at 0x08 which(w), 0x0A interval,
 * 0x0E value.  Its callers reserve a two-byte Pascal result slot at 0x12
 * (0x00E58D92 / 0x00E58F1A "subq.l #0x2,SP") and then pop all 0xC bytes
 * without reading it; the body never writes it either, so the result is not
 * modelled.
 */
void time_$get_itimer_internal(uint16_t which, clock_t *interval,
                               clock_t *value);

/*
 * time_$set_itimer_internal - arm/disarm TIME_$ITIMER_DB[which][PROC1_$AS_ID]
 *
 * 0x00E58D14, 324 bytes.  Six parameters at 0x08 which(w), 0x0A interval,
 * 0x0E value, 0x12 ointerval, 0x16 ovalue, 0x1A status, plus the same unused
 * two-byte result slot at 0x1E.
 */
void time_$set_itimer_internal(uint16_t which, clock_t *interval,
                               clock_t *value, clock_t *ointerval,
                               clock_t *ovalue, status_$t *status);

/*
 * Pascal by-reference constant cell at 0x00E16650 (0x000D000D), passed to
 * CRASH_SYSTEM by TIME_$WAIT (pea (0x14,PC) at 0x00E1663A) and TIME_$WAIT2
 * (pea (-0xa4,PC) at 0x00E166F2).  Defined in time/wait.c.
 */
extern const status_$t time_$c_queue_elem_in_use_crash;

/*
 * TIME_$TIMER_HANDLER - Hardware timer interrupt entry point
 *
 * 0x00E2B130..0x00E2B280 in the TIME_ASM segment, hand-written (movem all,
 * IO_$USE_INT_STACK, DI dispatch of TIME_$DI_VT / TIME_$DI_RTE).  Installed
 * by TIME_$SET_VECTOR into the level-6 autovector.  Not yet transcribed
 * (no Ghidra function at that address) - bead source-lu78.
 */
void TIME_$TIMER_HANDLER(void);

/*
 * The three timer callbacks below are reached through TIME_$Q_SCAN_QUEUE's
 * deferred path; time_$callback_arg_t is declared in time/time.h.  All three
 * read the callback_arg's low word as the address-space id
 * (movea.l (A0),A2 / move.w (0x2,A2),D0w).
 */

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
