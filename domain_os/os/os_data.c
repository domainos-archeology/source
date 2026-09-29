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
 *   OS_$SHUTDOWN_WAIT_TIME:  0xE82738 (6 bytes)   - Shutdown wait clock_t {3,0}
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
/*
 * The clock_t at 0xE82738 (+0x10 of the OS data segment, `D E82728 OS
 * size = 18`): OS_$SHUTDOWN hands it to TIME_$WAIT as the delay
 * (`pea (0x10,A5)` at 0x00E6D4CE).  Image bytes 00 00 00 03 00 00: three
 * high-word ticks, low word zero.
 */
clock_t OS_$SHUTDOWN_WAIT_TIME = { 0x00000003u, 0x0000u };

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
 * NULL_PC (0x00EB07FC) is a field of the STACK segment block below.
 */

/*
 * ============================================================================
 * The STACK segment
 * ============================================================================
 */

/*
 * OS_$STACK - map "D EB0000 STACK size = 2C00", 0x00EB0000..0x00EB2BFF
 * (layout, labels and asserts in os/os.h).  The image carries no bytes for
 * the segment, so the block is zero-filled; OS_$INIT fills NULL_PC and
 * clears the interrupt stack, and IO_$USE_INT_STACK writes IO_$SAVED_INT_SR.
 * Module data block OS_$STACK: Claude Opus 5.5 (source-4k71).
 */
MODULE_DATA_DEFINE(os_$stack_t, OS_$STACK, 0x00EB0000);

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
 * 0x00E81D20 (Ghidra label PTR_OS_DATA_SHUTWIRED_00e81d20), a cell of the
 * stopwatch module block at A5+0x50C rather than of this literal pool; both
 * cells hold 0x00E82128.  It is defined in stop/stop_data.c alongside the
 * rest of that block, so the two addresses are two C objects, as in the
 * image.
 */

/* 0x00E6D688 -> OS_DATA_SHUTWIRED (map: 0x00E82128) */
m68k_ptr_t PTR_OS_DATA_SHUTWIRED = 0x00E82128;

/* 0x00E6D68C -> OS_DATA_SHUTWIRED_END (map: 0x00E82740) */
m68k_ptr_t PTR_OS_DATA_SHUTWIRED_END = 0x00E82740;

/* 0x00E6D690 -> OS_PROC_SHUTWIRED (map: 0x00E5D050) */
m68k_ptr_t PTR_OS_PROC_SHUTWIRED = 0x00E5D050;

/* 0x00E6D694 -> OS_PROC_SHUTWIRED_END (map: 0x00E6D6FE) */
m68k_ptr_t PTR_OS_PROC_SHUTWIRED_END = 0x00E6D6FE;

