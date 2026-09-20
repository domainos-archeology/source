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

#include "proc1/proc1_internal.h"

/*
 * Current process state
 */
proc1_t *PROC1_$CURRENT_PCB = NULL;     /* Pointer to current process's PCB */
proc1_t *PROC1_$READY_PCB = NULL;       /* Head of the ready list */
/*
 * 0xE20608, inside the PROC1_ASM code segment; reached PC-relative from
 * proc1_$process_exit_handler (proc1/sau2/init_stack.s).  See
 * PROC1_ASM_DATA_SECTION in proc1/proc1_internal.h (source-uwxz).
 */
uint16_t PROC1_$CURRENT PROC1_ASM_DATA_SECTION = 0; /* PID of current process */

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
 * Original address: 0xE25D10 (A5 + 0x828); PROC1_$BIND clears entry pid at
 * +pid*16 (0x00E14DCA..0x00E14DD6).  Map: PROC1_$STATS 0xE25D20 = entry 1.
 */
uint32_t PROC_STATS_BASE[PROC1_MAX_PROCESSES * 4] = { 0 };

/*
 * ============================================================================
 * Timer Data
 * ============================================================================
 */

/*
 * Timeslice timer elements: A5 + 0x14 + pid*0x1C (0xE254FC for pid 0).
 * Only entries 1..64 are ever used; entry 0 overlaps PROC1_$LOADAV_ELEM in
 * the image (see the module map in proc1/proc1.h).  Zero in the image.
 */
proc1_ts_slot_t PROC1_$TS_ELEM[PROC1_MAX_PROCESSES];

/*
 * Timeslice values indexed by state
 * Original address: 0xE205D2
 */
/* Image bytes at 0xE205D2 (gsk read): ffff x7, 7d00 x4, 30d4 x5, ffff x2.
 * Lives in the PROC1_ASM code segment (map: E205D2 PROC1_$TSVV, before
 * PROC1_$SUSPEND_EC E205F6), reached PC-relative by ADVANCE_INT's
 * `lea (PROC1_$TSVV:w,%pc),%a0` at 0xE2078C, hence PROC1_ASM_DATA_SECTION. */
int16_t PROC1_$TSVV[PROC1_TSVV_COUNT] PROC1_ASM_DATA_SECTION = {
    -1, -1, -1, -1, -1, -1, -1,
    0x7D00, 0x7D00, 0x7D00, 0x7D00,
    0x30D4, 0x30D4, 0x30D4, 0x30D4, 0x30D4,
    -1, -1
};
_Static_assert(sizeof(PROC1_$TSVV) == 0xE205F6 - 0xE205D2, "PROC1_$TSVV extent");

/*
 * ============================================================================
 * Load Average Data
 * ============================================================================
 */

/*
 * The three load averages at A5 + 0 (0xE254E8), 8.24 fixed point, cleared by
 * PROC1_$INIT_LOADAV (0x00E14CA0) and rewritten by PROC1_$LOADAV_CALLBACK.
 */
int32_t PROC1_$LOADAV[PROC1_LOADAV_COUNT] = { 0, 0, 0 };

/*
 * The load-average timer element at A5 + 0x10 (0xE254F8).
 */
time_queue_elem_t PROC1_$LOADAV_ELEM;

/*
 * ============================================================================
 * Event Count / Suspend Data
 * ============================================================================
 */

/*
 * Suspend event count - signaled when a process is suspended
 * Original address: 0xE205F6
 */
ec_$eventcount_t PROC1_$SUSPEND_EC = { 0 };

/*
 * ============================================================================
 * Internal Timer Data
 * ============================================================================
 */

/*
 * PROC1_$VT_TIMER_DATA - the timer-index word at 0x00E14A06 (bytes 00 02);
 * see proc1/proc1_internal.h.
 */
const uint16_t PROC1_$VT_TIMER_DATA = 2;

/*
 * ============================================================================
 * Deferred-interrupt state and assembly-referenced constants
 * ============================================================================
 */

/*
 * DAT_00e20606 - "DI callback in progress" flag, the byte after DI_$Q_HEAD.
 * Zero in the image.  See proc1/proc1_internal.h.
 *
 * Original address: 0xE20606
 */
int8_t DAT_00e20606 = 0;

/*
 * Bad_atomic_operation_err - PROC1_$DISPATCH_INT's crash status.
 * Image bytes at 0x00E20DE8: 00 0a 00 07.
 *
 * Original address: 0xE20DE8
 */
status_$t Bad_atomic_operation_err = 0x000A0007;

/*
 * Illegal_process_id_err - the shared status cell at 0x00E152E0
 * (00 0a 00 01); see proc1/proc1_internal.h for its five `pea (d,PC)' users.
 */
const status_$t Illegal_process_id_err = status_$illegal_process_id;
