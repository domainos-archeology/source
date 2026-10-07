/*
 * time_data.c - TIME Module Global Data Definitions
 *
 * This file defines the global variables used by the TIME module.
 *
 * Original M68K addresses:
 *   TIME_$CLOCKH:         0xE2B0D4 (4 bytes)
 *   TIME_$CLOCKL:         0xE2B0E0 (2 bytes)
 *   TIME_$CURRENT_CLOCKH: 0xE2B0E4 (4 bytes)
 *   TIME_$CURRENT_CLOCKL: 0xE2B0E8 (2 bytes)
 *   TIME_$BOOT_TIME:      0xE2B0EC (4 bytes)
 *   TIME_$CURRENT_TIME:   0xE2B0F0 (4 bytes)
 *   TIME_$CURRENT_USEC:   0xE2B0F4 (4 bytes)
 *   TIME_$CURRENT_TICK:   0xE2B0F8 (2 bytes)
 *   TIME_$CURRENT_SKEW:   0xE2B0FA (2 bytes)
 *   TIME_$CURRENT_DELTA:  0xE2B0FC (4 bytes)
 *   IN_VT_INT:            0xE2AF6A (1 byte)
 *   IN_RT_INT:            0xE2AF6B (1 byte)
 *   TIME_$RTEQ:           0xE2A7A0 (12 bytes)
 *   TIME_$DI_VT:          0xE2B10E (16 bytes)
 *   TIME_$DI_RTE:         0xE2B11E (16 bytes)
 */

#include "time/time_internal.h"

/*
 * ============================================================================
 * Clock Values
 * ============================================================================
 */

/*
 * Absolute clock (adjusted for drift/skew)
 *
 * This is the "official" time returned by TIME_$ABS_CLOCK.
 *
 * Original addresses: 0xE2B0D4 (high), 0xE2B0E0 (low)
 */
ec_$eventcount_t TIME_$CLOCKH_EC = {                 /* 0xE2B0D4..0xE2B0E0 */
    .value = 0,                                      /* TIME_$CLOCKH */
    .waiter_list_head = (ec_$eventcount_waiter_t *)&TIME_$CLOCKH_EC,
    .waiter_list_tail = (ec_$eventcount_waiter_t *)&TIME_$CLOCKH_EC,
};
_Static_assert(sizeof(TIME_$CLOCKH_EC) == 0x0C,
               "TIME_$CLOCKH_EC: 0x00E2B0D4..0x00E2B0E0 (TIME_$CLOCKL)");
uint16_t TIME_$CLOCKL = 0;

/*
 * Current clock (raw, unadjusted)
 *
 * This is the raw clock value from TIME_$CLOCK.
 *
 * Original addresses: 0xE2B0E4 (high), 0xE2B0E8 (low)
 */
uint32_t TIME_$CURRENT_CLOCKH = 0;
uint16_t TIME_$CURRENT_CLOCKL = 0;

/*
 * Boot time
 *
 * Clock value captured at system boot.
 *
 * Original address: 0xE2B0EC
 */
uint32_t TIME_$BOOT_TIME = 0;

/*
 * ============================================================================
 * Time of Day
 * ============================================================================
 */

/*
 * Current time of day (seconds since epoch)
 *
 * Stored as Unix time + 0x12CEA600 offset (Apollo epoch adjustment).
 *
 * Original address: 0xE2B0F0
 */
uint32_t TIME_$CURRENT_TIME = 0;

/*
 * Current microseconds within second
 *
 * Original address: 0xE2B0F4
 */
uint32_t TIME_$CURRENT_USEC = 0;

/*
 * ============================================================================
 * Timer State
 * ============================================================================
 */

/*
 * Current tick counter
 *
 * Initialized to 0x1047 at boot.
 *
 * Original address: 0xE2B0F8
 */
uint16_t TIME_$CURRENT_TICK = 0;

/*
 * Clock adjustment values
 *
 * Used for gradual time adjustment (adjtime-style).
 *
 * Original addresses: 0xE2B0FA (skew), 0xE2B0FC (delta)
 */
uint16_t TIME_$CURRENT_SKEW = 0;
uint32_t TIME_$CURRENT_DELTA = 0;

/*
 * time_$mcr_countdown - 0x00E2B100, the word after TIME_$CURRENT_DELTA and
 * before TIME_$SET_VECTOR's code (no map symbol).  TIME_$TIMER_HANDLER
 * decrements it on every CLOCKH step and, reaching zero, reloads 2 and
 * calls MMU_$MCR_CHANGE(7) (0x00E2B1DE-0x00E2B1F4).
 * `gsk read 0xE2B100 2` = 00 02.
 */
uint16_t time_$mcr_countdown = 0x0002;

/*
 * ============================================================================
 * Interrupt Flags
 * ============================================================================
 */

/*
 * Virtual timer interrupt in progress flag
 *
 * Original address: 0xE2AF6A
 */
uint8_t IN_VT_INT = 0;

/*
 * Real-time timer interrupt in progress flag
 *
 * Original address: 0xE2AF6B
 */
uint8_t IN_RT_INT = 0;

/*
 * ============================================================================
 * Queue Structures
 * ============================================================================
 */

/*
 * Virtual-timer event queues, one per process (Pascal 1-based).
 *
 * Original address: 0xE2A4A0, 64 entries of 12 bytes, ending exactly at
 * TIME_$RTEQ (0xE2A7A0).  See the comment on the declaration in time/time.h
 * for the four instruction sequences that address it.
 */
time_queue_t TIME_$VTQ[TIME_MAX_PROCESSES];

/*
 * Real-time event queue
 *
 * Main queue for real-time timer events.
 *
 * Original address: 0xE2A7A0 (base 0xE29198 + offset 0x1608)
 */
time_queue_t TIME_$RTEQ = { 0, 0, 0, 0, 0 };

/*
 * Deferred interrupt queue elements
 *
 * Used to defer timer interrupt processing.
 *
 * Original addresses: 0xE2B10E (VT), 0xE2B11E (RTE)
 */
di_queue_elem_t TIME_$DI_VT = { 0, 0, 0, 0 };
di_queue_elem_t TIME_$DI_RTE = { 0, 0, 0, 0 };

/*
 * TIME_$FAST_CLOCK_EC - fast-clock eventcount (0x00E2B0C8, 0x0C bytes).
 *
 * Registered with EC2_$REGISTER_EC1 by TIME_$GET_EC for ec_id 1.  The image
 * value is the EC_$INIT'd state: value 0 and an empty circular waiter list
 * whose head and tail both point back at the eventcount.
 */
ec_$eventcount_t TIME_$FAST_CLOCK_EC = {
    .value = 0,
    .waiter_list_head = (ec_$eventcount_waiter_t *)&TIME_$FAST_CLOCK_EC,
    .waiter_list_tail = (ec_$eventcount_waiter_t *)&TIME_$FAST_CLOCK_EC,
};
_Static_assert(sizeof(TIME_$FAST_CLOCK_EC) == 0x0C,
               "TIME_$FAST_CLOCK_EC: 0x00E2B0C8..0x00E2B0D4 (TIME_$CLOCKH_EC)");

/*
 * Unnamed TIME_ data-segment cells, see time_internal.h.
 *
 * `gsk read 0x00E2A7AC 16`: all zero.  time_$zero_interval occupies
 * 0xE2A7AC..0xE2A7B2; the two handles are at 0xE2A7B4 and 0xE2A7B8.
 */
clock_t time_$zero_interval = { 0, 0 };
void *time_$fast_clock_ec_handle = NULL;
void *time_$clock_ec_handle = NULL;
_Static_assert(sizeof(time_$zero_interval) == 6,
               "time_$zero_interval: 0x00E2A7AC..0x00E2A7B2");
