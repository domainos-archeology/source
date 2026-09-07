/*
 * os_data.c - OS Module Global Data Definitions
 *
 * This file defines the global variables used by the OS core module.
 *
 * Original M68K addresses:
 *   OS_$REV:                 0xE78400 (204 bytes) - OS revision info
 *   OS_$SHUTDOWN_EC:         0xE1DC00 (12 bytes)  - Shutdown eventcount
 *   OS_$BOOT_DEVICE:         0xE82728 (8 bytes)   - Boot device record
 *   OS_$SHUTTING_DOWN_FLAG:  0xE82734 (1 byte)    - Shutdown in progress
 *   OS_$SHUTDOWN_WAIT_TIME:  0xE82738 (4 bytes)   - Shutdown wait time
 */

#include "os/os_internal.h"

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
 * Boot device record
 *
 * Identifies the device, controller and unit the system was booted from.
 *
 * Original address: 0xE82728
 */
os_$boot_device_t OS_$BOOT_DEVICE = {0, 0, 0, 0};

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

/*
 * ============================================================================
 * Boot info table (the OS module's static block)
 * ============================================================================
 */

/*
 * BOOT_INFO_TABLE - 0x00E351F4, the block OS_$INIT runs with in A5
 * (`lea (0xe351f4).l,A5` at 0x00E337FC).  The map segment is
 * "D E351F4 OS size = 188" and it exports no interior symbol, so the whole
 * 0x188 bytes are this one module-local table; the next segment,
 * "D E3537C SMD size = 4", starts exactly 0x188 bytes later.  98 longwords.
 *
 * os_$install_vectors walks entries 1..0x28 and reads each one's `(0xdc,A0)`
 * neighbour, i.e. index + 0x37, so the highest index it touches is 0x5F --
 * inside the 98 the segment holds.  The image is all zeroes; the bootstrap
 * fills the table in before jumping to OS_$INIT.
 */
uint32_t BOOT_INFO_TABLE[0x188 / 4] = { 0 };
_Static_assert(sizeof(BOOT_INFO_TABLE) == 0x188,
               "BOOT_INFO_TABLE: map segment OS 0x00E351F4 size = 188");

/*
 * ============================================================================
 * Null process
 * ============================================================================
 */

/*
 * NULL_PC - 0x00EB07FC.  OS_$INIT stores the address of NULLPROC here
 * (os/sau2/nullproc.s; the map has it as "D E24C60 NULLPROC size = 18").
 * The cell lives in the zero-filled OS_PAGE region, so the image carries no
 * bytes for it and it starts at zero.
 */
void *NULL_PC = NULL;

/*
 * ============================================================================
 * Shutdown wiring pointer cells
 * ============================================================================
 *
 * OS_$SHUTDOWN makes two MST_$WIRE_AREA calls, each handed the addresses of a
 * start and an end cell.  The four cells are consecutive longwords in the
 * literal pool at the tail of OS_$SHUTDOWN's code, 0x00E6D688..0x00E6D697,
 * and each holds the address the SAU2 map gives for the matching region.
 *
 * STOP_$WATCH carries its own copy of the OS_DATA_SHUTWIRED start pointer at
 * 0x00E81D20, inside its frame record (stop/stop_internal.h documents it at
 * +0x50C); both cells hold 0x00E82128, and stop/watch.c links against the
 * definition below.
 * TODO(source-wk2f, 0x00E81D20): give STOP_$WATCH's copy its own definition
 * inside the stop module block so the two cells are not conflated.
 */

/* 0x00E6D688 -> OS_DATA_SHUTWIRED (map: 0x00E82128) */
m68k_ptr_t PTR_OS_DATA_SHUTWIRED = 0x00E82128;

/* 0x00E6D68C -> OS_DATA_SHUTWIRED_END (map: 0x00E82740) */
m68k_ptr_t PTR_OS_DATA_SHUTWIRED_END = 0x00E82740;

/* 0x00E6D690 -> OS_PROC_SHUTWIRED (map: 0x00E5D050) */
m68k_ptr_t PTR_OS_PROC_SHUTWIRED = 0x00E5D050;

/* 0x00E6D694 -> OS_PROC_SHUTWIRED_END (map: 0x00E6D6FE) */
m68k_ptr_t PTR_OS_PROC_SHUTWIRED_END = 0x00E6D6FE;

