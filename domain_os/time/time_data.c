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
uint32_t TIME_$CLOCKH = 0;
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
