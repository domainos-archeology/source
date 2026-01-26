/*
 * os_data.c - OS Module Global Data Definitions
 *
 * This file defines the global variables used by the OS core module.
 *
 * Original M68K addresses:
 *   OS_$REV:                 0xE78400 (204 bytes) - OS revision info
 *   OS_$SHUTDOWN_EC:         0xE1DC00 (12 bytes)  - Shutdown eventcount
 *   OS_$BOOT_DEVICE:         0xE82728 (2 bytes)   - Boot device ID
 *   OS_$SHUTTING_DOWN_FLAG:  0xE82734 (1 byte)    - Shutdown in progress
 *   OS_$SHUTDOWN_WAIT_TIME:  0xE82738 (4 bytes)   - Shutdown wait time
 */

#include "os/os.h"

/*
 * ============================================================================
 * Revision Information
 * ============================================================================
 */

/*
 * OS revision information array
 *
 * Contains version strings, build dates, and other revision info.
 * Structure is 0x33 longwords (204 bytes).
 *
 * Original address: 0xE78400
 */
uint32_t OS_$REV[51] = { 0 };

/*
 * ============================================================================
 * Shutdown State
 * ============================================================================
 */

/*
 * Shutdown eventcount
 *
 * Registered eventcount that processes can wait on to be notified
 * when system shutdown begins.
 *
 * Original address: 0xE1DC00 (part of OS_DATA_WIRED section)
 */
ec_$eventcount_t OS_$SHUTDOWN_EC = { 0 };

/*
 * Boot device identifier
 *
 * Identifies the device from which the system was booted.
 *
 * Original address: 0xE82728
 */
uint16_t OS_$BOOT_DEVICE = 0;

/*
 * Shutdown in progress flag
 *
 * Set to 0xFF when OS_$SHUTDOWN begins. Used by various subsystems
 * to check if the system is shutting down.
 *
 * Original address: 0xE82734
 */
char OS_$SHUTTING_DOWN_FLAG = 0;

/*
 * Shutdown wait time
 *
 * Time value used during shutdown sequence for waiting on subsystems.
 * Initialized to 3 (presumably clock ticks or some time unit).
 *
 * Original address: 0xE82738
 */
uint32_t OS_$SHUTDOWN_WAIT_TIME = 3;
