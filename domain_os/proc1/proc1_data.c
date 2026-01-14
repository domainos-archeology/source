/*
 * proc1_data.c - PROC1 Global Data Definitions
 *
 * This file defines the global variables used by the process management
 * subsystem. On the original M68K hardware, these were at fixed addresses.
 * For portability, we define them as regular variables here.
 *
 * Original M68K addresses (SAU2):
 *   PROC1_$CURRENT_PCB:     0xE1EAC8
 *   PROC1_$READY_PCB:       PC-relative from dispatch code
 *   PROC1_$CURRENT:         0xE20608
 *   PROC1_$READY_COUNT:     0xE1EBD0
 *   PROC1_$ATOMIC_OP_DEPTH: 0xE2060E
 *   PROC1_$AS_ID:           0xE2060A
 *   PCBS:                   0xE1EACC (65 pointers)
 *   PROC1_$TYPE:            0xE2612A (65 uint16_t values)
 */

#include "proc1.h"

/*
 * Current process state
 */
proc1_t *PROC1_$CURRENT_PCB = NULL;     /* Pointer to current process's PCB */
proc1_t *PROC1_$READY_PCB = NULL;       /* Head of the ready list */
uint16_t PROC1_$CURRENT = 0;            /* PID of current process */

/*
 * Ready list tracking
 */
uint16_t PROC1_$READY_COUNT = 0;        /* Number of processes in ready list */

/*
 * Atomic operation and address space state
 */
uint16_t PROC1_$ATOMIC_OP_DEPTH = 0;    /* Nesting depth of atomic operations */
uint16_t PROC1_$AS_ID = 0;              /* Current address space ID */

/*
 * Process Control Block (PCB) table
 *
 * Array of pointers to PCBs, indexed by PID.
 * Size determined by PROC1_MAX_PROCESSES from proc1_config.h.
 *
 * PID allocation:
 *   0: Reserved/invalid
 *   1: System process
 *   2: Idle/init process
 *   3-64: User processes (on SAU2)
 */
proc1_t *PCBS[PROC1_MAX_PROCESSES] = { NULL };

/*
 * Process type table
 *
 * Stores the type code for each process, indexed by PID.
 * Used by PROC1_$GET_TYPE and PROC1_$SET_TYPE.
 *
 * Known type values:
 *   0: Unbound/invalid
 *   3: Kernel daemon
 *   4-5, 10: Other system types (ws_param = 5)
 *   8: Special system type (ws_param = 6)
 *
 * Original address: 0xE2612A
 */
uint16_t PROC1_$TYPE[PROC1_MAX_PROCESSES] = { 0 };

/*
 * ============================================================================
 * Stack Allocation Data
 * ============================================================================
 */

/*
 * Stack allocation pointers
 *
 * Original addresses:
 *   STACK_FREE_LIST:  0xE26120 (base + 0xc38)
 *   STACK_HIGH_WATER: 0xE26124 (base + 0xc3c)
 *   STACK_LOW_WATER:  0xE26128 (base + 0xc40)
 */
void *STACK_FREE_LIST = NULL;           /* Free list of 4KB stacks */
void *STACK_HIGH_WATER = NULL;          /* High water mark (grows down) */
void *STACK_LOW_WATER = NULL;           /* Low water mark (grows up) */

/*
 * OS stack table - one stack per process
 * Original address: 0xE25C18 (base + 0x730)
 */
void *OS_STACK_BASE[PROC1_MAX_PROCESSES] = { NULL };

/*
 * Process statistics table - 16 bytes per process (4 uint32_t values)
 * Original address: 0xE25D10
 */
uint32_t PROC_STATS_BASE[PROC1_MAX_PROCESSES * 4] = { 0 };

/*
 * ============================================================================
 * Timer Data
 * ============================================================================
 */

/*
 * Timer callback entry table (28 bytes per process)
 * Original address: 0xE254E8
 *
 * This is a large data block containing:
 * - Load average values at offset 0x00-0x0B
 * - Timer callback entries for each process
 *
 * Note: ts_timer_entry_t is defined in proc1.h
 */
ts_timer_entry_t TS_TIMER_TABLE[PROC1_MAX_PROCESSES];

/*
 * Timer queue elements - 12 bytes per process
 * Original address: 0xE2A494
 */
char TS_QUEUE_TABLE[PROC1_MAX_PROCESSES * 12];

/*
 * Timeslice values indexed by state
 * Original address: 0xE205D2
 * Array of int16_t values for each priority/state level
 *
 * Note: PROC1_MAX_STATES is defined in proc1.h
 */
int16_t TIMESLICE_TABLE[PROC1_MAX_STATES] = { 0 };

/*
 * ============================================================================
 * Load Average Data
 * ============================================================================
 *
 * Note: Load average data shares the same memory block as timer data
 * starting at 0xE254E8. These are separate variables for clarity.
 */
int32_t LOADAV_1MIN = 0;                /* 1-minute load average */
int32_t LOADAV_5MIN = 0;                /* 5-minute load average */
int32_t LOADAV_15MIN = 0;               /* 15-minute load average */

/*
 * Load average callback entry
 * Uses part of the TS_TIMER_TABLE structure
 */
/* LOADAV_BASE is a pointer into the timer table, not a separate allocation */

/*
 * ============================================================================
 * Event Count / Suspend Data
 * ============================================================================
 */

/*
 * Suspend event count - signaled when a process is suspended
 * Original address: 0xE205F6
 */
uint32_t PROC1_$SUSPEND_EC = 0;
