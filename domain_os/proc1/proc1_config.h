/*
 * proc1_config.h - PROC1 Configuration Constants
 *
 * This file contains configuration constants for the process management
 * subsystem that may vary by SAU (System Architecture Unit) type.
 *
 * For SAU2 (the current implementation), these values are derived from
 * the original binary analysis.
 */

#ifndef PROC1_CONFIG_H
#define PROC1_CONFIG_H

/*
 * SAU2 Configuration
 *
 * These values apply to the SAU2 (System Architecture Unit type 2)
 * which is the Apollo DN3000/DN4000/DN4500 series.
 *
 * TODO: Add #if blocks for other SAU types when they are analyzed:
 *   - SAU3: DN10000
 *   - SAU5: DSP series
 *   - SAU7: DN5500/DN10000VS
 *   - SAU8: Series 400
 */

/*
 * Maximum number of level-1 processes
 *
 * This defines the size of the PCB table and related arrays.
 * PIDs range from 0 to (PROC1_MAX_PROCESSES - 1).
 *
 * SAU2 value: 65 (0x41), allowing PIDs 0-64
 * - PID 0: Reserved/invalid
 * - PID 1: System process
 * - PID 2: Idle/init process
 * - PIDs 3-64: User processes
 */
#define PROC1_MAX_PROCESSES     65

/*
 * Stack allocation region boundaries
 *
 * These define the virtual address ranges used for process stack allocation.
 * The low region grows upward, the high region grows downward.
 */
#define PROC1_STACK_LOW_START   0x00D00000
#define PROC1_STACK_HIGH_START  0x00D50000

/*
 * OS stack base address
 * Base address for system/kernel stacks
 */
#define PROC1_OS_STACK_BASE     0x00EB2000

/*
 * Stack page size and minimum large stack threshold
 */
#define PROC1_STACK_PAGE_SIZE   0x0400      /* 1KB pages */
#define PROC1_STACK_LARGE_MIN   0x1000      /* 4KB threshold */

#endif /* PROC1_CONFIG_H */
