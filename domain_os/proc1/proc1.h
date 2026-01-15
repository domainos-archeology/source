/*
 * PROC1 - Process Management Module
 *
 * This module provides process management for Domain/OS including:
 * - Process creation, binding, and termination
 * - Scheduling and ready list management
 * - Context switching (dispatcher)
 * - Lock management (resource locks)
 * - Atomic operations and inhibit regions
 * - CPU time tracking and load averaging
 *
 * Memory layout (m68k):
 *   - PCB table: 0xE1EACC (array of pointers, max 65 processes)
 *   - Current PCB: 0xE1EAC8
 *   - Current PID: 0xE20608
 *   - Ready PCB: PC-relative from dispatch code
 *   - Ready count: 0xE1EBD0
 *   - Atomic depth: 0xE2060E
 */

#ifndef PROC1_H
#define PROC1_H

#include "base/base.h"
#include "ec/ec.h"
#include "proc1/proc1_config.h"

/*
 * Process Control Block (PCB) structure
 * Size: ~0x68 bytes (104 bytes)
 *
 * The ready list is a doubly-linked circular list ordered by
 * resource_locks_held (descending) then state (ascending).
 */
typedef struct proc1_t {
    struct proc1_t *nextp;          /* 0x00: Next process in ready list */
    struct proc1_t *prevp;          /* 0x04: Previous process in ready list */

    /* Saved registers (context switch) */
    uint32_t    save_d2;            /* 0x08: Saved D2 */
    uint32_t    save_d3;            /* 0x0C: Saved D3 */
    uint32_t    save_d4;            /* 0x10: Saved D4 */
    uint32_t    save_d5;            /* 0x14: Saved D5 */
    uint32_t    save_d6;            /* 0x18: Saved D6 */
    uint32_t    save_d7;            /* 0x1C: Saved D7 */
    uint32_t    save_a2;            /* 0x20: Saved A2 */
    uint32_t    save_a3;            /* 0x24: Saved A3 */
    uint32_t    save_a4;            /* 0x28: Saved A4 */
    uint32_t    save_a5;            /* 0x2C: Saved A5 */
    uint32_t    save_a6;            /* 0x30: Saved A6 */
    uint32_t    save_a7;            /* 0x34: Saved A7 (SSP) */
    void        *save_usp;          /* 0x38: Saved user stack pointer */

    uint32_t    wait_start;         /* 0x3C: TIME_$CLOCKH when wait started */
    uint32_t    resource_locks_held;/* 0x40: Bitmask of held resource locks */

    uint16_t    mypid;              /* 0x44: Process ID */
    uint16_t    asid;               /* 0x46: Address Space ID */
    int16_t     vtimer;             /* 0x48: Virtual timer value */
    uint16_t    pad_4a;             /* 0x4A: Padding or reserved */
    uint32_t    cpu_total;          /* 0x4C: CPU time high word */
    uint16_t    cpu_usage;          /* 0x50: CPU time low word */

    uint16_t    state;              /* 0x52: Process state */
    uint8_t     pri_min;            /* 0x54: Minimum priority or flags */
    uint8_t     pri_max;            /* 0x55: Priority/flags byte */
                                    /*   Bit 0 (0x01): Waiting on EC */
                                    /*   Bit 1 (0x02): Suspended */
                                    /*   Bit 2 (0x04): Deferred suspend? */
                                    /*   Bit 3 (0x08): Bound (in use) */

    uint16_t    inh_count;          /* 0x56: Inhibit count? */
    uint16_t    sw_bsr;             /* 0x58: Software base/something */
    uint16_t    pad_5a;             /* 0x5A: Padding */

    uint32_t    field_5c;           /* 0x5C: Unknown */
    uint32_t    field_60;           /* 0x60: Unknown */
    uint32_t    field_64;           /* 0x64: Unknown */
} proc1_t;

/*
 * PCB flag bits (in pri_max at offset 0x55)
 */
#define PROC1_FLAG_WAITING      0x01    /* Waiting on event count */
#define PROC1_FLAG_SUSPENDED    0x02    /* Process is suspended */
#define PROC1_FLAG_DEFER_SUSP   0x04    /* Deferred suspend pending */
#define PROC1_FLAG_BOUND        0x08    /* Process is bound (in use) */

/*
 * Lock IDs for PROC1_$SET_LOCK / PROC1_$CLR_LOCK
 * Locks are implemented as bits in resource_locks_held
 */
#define PROC1_CREATE_LOCK_ID    0x0B    /* Process creation lock */

/*
 * Status codes
 */
#define status_$illegal_process_id          0x000A0001
#define status_$no_pcb_is_available         0x000A0008
#define status_$process_not_bound           0x000A0005
#define status_$process_not_suspended       0x000A0003
#define status_$process_already_suspended   0x000A0004

/*
 * Timer callback entry structure (28 bytes per process)
 * Original address: 0xE254E8
 */
typedef struct ts_timer_entry_t {
    uint32_t field_00;
    uint32_t field_04;
    uint32_t field_08;
    uint32_t field_0c;
    uint32_t field_10;
    void     *callback_info;    /* 0x14 */
    void     (*callback)(void*);/* 0x18 */
    uint32_t callback_param;    /* 0x1C */
    uint32_t cpu_time_high;     /* 0x20 */
    uint16_t cpu_time_low;      /* 0x24 */
    uint16_t field_26;          /* 0x26 */
} ts_timer_entry_t;

/*
 * Maximum number of state/priority levels for timeslice table
 */
#define PROC1_MAX_STATES 32

/*
 * ============================================================================
 * Global Variables
 * ============================================================================
 *
 * These are defined in proc1_data.c with appropriate sizes.
 * The original M68K addresses are documented for reference.
 */

/*
 * Core process state (M68K addresses in comments)
 */
extern proc1_t *PROC1_$CURRENT_PCB;     /* 0xE1EAC8: Current running process */
extern proc1_t *PROC1_$READY_PCB;       /* PC-relative: Head of ready list */
#ifndef PROC1_$CURRENT
extern uint16_t PROC1_$CURRENT;         /* 0xE20608: Current process ID */
#endif
extern uint16_t PROC1_$READY_COUNT;     /* 0xE1EBD0: Number of ready processes */
extern uint16_t PROC1_$ATOMIC_OP_DEPTH; /* 0xE2060E: Atomic operation nesting */
#ifndef PROC1_$AS_ID
extern uint16_t PROC1_$AS_ID;           /* 0xE2060A: Current address space ID */
#endif
extern proc1_t *PCBS[PROC1_MAX_PROCESSES];      /* 0xE1EACC: PCB pointer table */
#ifndef PROC1_$TYPE
extern uint16_t PROC1_$TYPE[PROC1_MAX_PROCESSES]; /* 0xE2612A: Process types */
#endif

/*
 * Stack allocation (M68K base: 0xE254E8)
 */
extern void *STACK_FREE_LIST;           /* 0xE26120: Free list of 4KB stacks */
extern void *STACK_HIGH_WATER;          /* 0xE26124: High water (grows down) */
extern void *STACK_LOW_WATER;           /* 0xE26128: Low water (grows up) */
extern void *OS_STACK_BASE[PROC1_MAX_PROCESSES]; /* 0xE25C18: OS stacks */

/*
 * Process statistics - 16 bytes per process (4 uint32_t values)
 * Original address: 0xE25D10
 */
extern uint32_t PROC_STATS_BASE[PROC1_MAX_PROCESSES * 4];

/*
 * Timer data
 */
extern ts_timer_entry_t TS_TIMER_TABLE[PROC1_MAX_PROCESSES]; /* 0xE254E8 */
extern char TS_QUEUE_TABLE[PROC1_MAX_PROCESSES * 12];        /* 0xE2A494 */
extern int16_t TIMESLICE_TABLE[PROC1_MAX_STATES];            /* 0xE205D2 */

/*
 * Load average data
 */
extern int32_t LOADAV_1MIN;             /* 1-minute load average */
extern int32_t LOADAV_5MIN;             /* 5-minute load average */
extern int32_t LOADAV_15MIN;            /* 15-minute load average */

/*
 * Event count for process suspension
 */

extern ec_$eventcount_t PROC1_$SUSPEND_EC; /* 0xE205F6: Suspend event count */

extern void INIT_STACK(proc1_t *pcb, void *params);

/*
 * ============================================================================
 * Process Lifecycle Functions
 * ============================================================================
 */

/*
 * PROC1_$INIT - Initialize process subsystem
 * Original address: 0x00e2f958
 */
void PROC1_$INIT(void);

/*
 * PROC1_$CREATE_P - Create a new process
 * Parameters:
 *   funcptr - Entry point function
 *   type - Process type (packed: low=stack type, high=proc type)
 *   status_ret - Status return
 * Returns: Process ID
 * Original address: 0x00e15148
 */
uint16_t PROC1_$CREATE_P(void *funcptr, uint32_t type, status_$t *status_ret);

/*
 * PROC1_$BIND - Bind a process to a PCB
 * Original address: 0x00e14d1c
 */
uint16_t PROC1_$BIND(void *proc_startup, void *stack1, void *stack2,
                     uint16_t ws_param, status_$t *status_p);

/*
 * PROC1_$UNBIND - Unbind a process
 * Original address: 0x00e14e24
 */
void PROC1_$UNBIND(uint16_t pid, status_$t *status_ret);

/*
 * PROC1_$ALLOC_STACK - Allocate a process stack
 * Original address: 0x00e1501a
 */

void *PROC1_$ALLOC_STACK(uint16_t type, status_$t *status_ret);

/*
 * PROC1_$FREE_STACK - Free a process stack
 * Original address: 0x00e1511a
 */
void PROC1_$FREE_STACK(void *stack);

/*
 * ============================================================================
 * Suspend/Resume Functions
 * ============================================================================
 */

/*
 * PROC1_$SUSPEND - Suspend a process
 * Original address: 0x00e147fa
 */
int8_t PROC1_$SUSPEND(uint16_t process_id, status_$t *status_ret);

/*
 * PROC1_$SUSPENDP - Suspend with parameters
 * Original address: 0x00e14876
 */
void PROC1_$SUSPENDP(uint16_t pid, status_$t *status_ret);

/*
 * PROC1_$RESUME - Resume a suspended process
 * Original address: 0x00e1476e
 */
void PROC1_$RESUME(uint16_t pid, status_$t *status_p);

/*
 * PROC1_$TRY_TO_SUSPEND - Internal: attempt to suspend
 * Original address: 0x00e1471c
 */
void PROC1_$TRY_TO_SUSPEND(proc1_t *pcb);

/*
 * ============================================================================
 * Dispatcher Functions
 * ============================================================================
 */

/*
 * PROC1_$DISPATCH - High-level dispatch
 * Original address: 0x00e20a18
 */
void PROC1_$DISPATCH(void);

/*
 * PROC1_$DISPATCH_INT - Dispatch (internal, A1=current PCB)
 * Assembly function that saves context and switches processes.
 * Original address: 0x00e20a20
 */
void PROC1_$DISPATCH_INT(void);

/*
 * PROC1_$DISPATCH_INT2 - Dispatch variant (A1=pcb param)
 * Original address: 0x00e20a24
 */
void PROC1_$DISPATCH_INT2(proc1_t *pcb);

/*
 * PROC1_$DISPATCH_INT3 - Full context switch implementation
 * Original address: 0x00e20a34
 */
void PROC1_$DISPATCH_INT3(void);

/*
 * ============================================================================
 * Ready List Functions
 * ============================================================================
 */

/*
 * PROC1_$ADD_READY - Add process to ready list
 * Original address: 0x00e20820
 */
void PROC1_$ADD_READY(proc1_t *pcb);

/*
 * PROC1_$REMOVE_READY - Remove process from ready list
 * Original address: 0x00e206d2
 */
void PROC1_$REMOVE_READY(proc1_t *pcb);

/*
 * PROC1_$REORDER_READY - Reorder process in ready list
 * Original address: 0x00e207d4
 */
void PROC1_$REORDER_READY(void);

/*
 * proc1_$remove_from_ready_list - Internal remove helper
 * Original address: 0x00e206d6
 */
void proc1_$remove_from_ready_list(proc1_t *pcb);

/*
 * proc1_$insert_into_ready_list - Internal insert helper
 * Original address: 0x00e20844
 */
void proc1_$insert_into_ready_list(proc1_t *pcb);

/*
 * proc1_$reorder_if_needed - Reorder if priority changed
 * Original address: 0x00e207d8
 */
void proc1_$reorder_if_needed(proc1_t *pcb);

/*
 * ============================================================================
 * Lock Functions
 * ============================================================================
 */

/*
 * PROC1_$SET_LOCK - Acquire a resource lock
 * Original address: 0x00e20ae4
 */
void PROC1_$SET_LOCK(uint16_t lock_id);

/*
 * PROC1_$CLR_LOCK - Release a resource lock
 * Original address: 0x00e20b92
 */
void PROC1_$CLR_LOCK(uint16_t lock_id);

/*
 * PROC1_$TST_LOCK - Test if a lock is held
 * Original address: 0x00e148ca
 */
int16_t PROC1_$TST_LOCK(uint16_t lock_id);

/*
 * PROC1_$GET_LOCKS - Get locks held by current process
 * Original address: 0x00e148e6
 */
uint32_t PROC1_$GET_LOCKS(void);

/*
 * ============================================================================
 * Atomic Operation Functions
 * ============================================================================
 */

/*
 * PROC1_$BEGIN_ATOMIC_OP - Begin atomic operation region
 * Original address: 0x00e209e6
 */
void PROC1_$BEGIN_ATOMIC_OP(void);

/*
 * PROC1_$END_ATOMIC_OP - End atomic operation region
 * Original address: 0x00e209fa
 */
void PROC1_$END_ATOMIC_OP(void);

/*
 * PROC1_$INHIBIT_BEGIN - Begin inhibit region
 * Original address: 0x00e20efc
 */
void PROC1_$INHIBIT_BEGIN(void);

/*
 * PROC1_$INHIBIT_END - End inhibit region
 * Original address: 0x00e20ea2
 */
void PROC1_$INHIBIT_END(void);

/*
 * PROC1_$INHIBIT_CHECK - Check inhibit state
 * Parameters:
 *   pcb - Process to check
 * Returns:
 *   -1 if process is inhibited (inh_count != 0)
 *   0 if not inhibited
 * Original address: 0x00e20ef0
 */
int8_t PROC1_$INHIBIT_CHECK(proc1_t *pcb);

/*
 * ============================================================================
 * Event Count Integration
 * ============================================================================
 */

/*
 * PROC1_$EC_WAITN - Wait on event counts (internal)
 * Original address: 0x00e2065a
 */
uint16_t PROC1_$EC_WAITN(proc1_t *pcb, ec_$eventcount_t **ecs,
                          int32_t *wait_vals, int16_t num_ecs);

/*
 * ============================================================================
 * CPU Time and Load Functions
 * ============================================================================
 */

/*
 * PROC1_$GET_CPUT - Get CPU time for process
 * Original address: 0x00e20894
 */
void PROC1_$GET_CPUT(uint16_t pid, void *time);

/*
 * PROC1_$GET_CPUT8 - Get CPU time as 48-bit clock_t
 *
 * Returns the CPU time for the current process as a 48-bit clock value.
 *
 * Parameters:
 *   cpu_time - Pointer to receive 48-bit CPU time value
 */
void PROC1_$GET_CPUT8(clock_t *cpu_time);

/*
 * PROC1_$GET_CPU_USAGE - Get CPU usage
 * Original address: 0x00e208aa
 */
void PROC1_$GET_CPU_USAGE(uint16_t pid, void *usage);

/*
 * PROC1_$GET_LOADAV - Get system load average
 * Original address: 0x00e14bba
 */
void PROC1_$GET_LOADAV(void *loadav);

/*
 * PROC1_$INIT_LOADAV - Initialize load averaging
 * Original address: 0x00e14c94
 */
void PROC1_$INIT_LOADAV(void);

/*
 * ============================================================================
 * Priority and Type Functions
 * ============================================================================
 */

/*
 * PROC1_$SET_PRIORITY - Set process priority range
 * Sets min and max priority for process if mode < 0.
 * Original address: 0x00e1523c
 */
void PROC1_$SET_PRIORITY(uint16_t pid, int16_t mode, uint16_t *min_priority, uint16_t *max_priority);

/*
 * PROC1_$SET_TYPE - Set process type
 * Original address: 0x00e152e4
 */
void PROC1_$SET_TYPE(uint16_t pid, uint16_t type);

/*
 * PROC1_$GET_TYPE - Get process type
 * Original address: 0x00e15324
 */
uint16_t PROC1_$GET_TYPE(uint16_t pid);

/*
 * ============================================================================
 * Virtual Timer Functions
 * ============================================================================
 */

/*
 * PROC1_$SET_VT - Set virtual timer
 * Original address: 0x00e1495c
 */
void PROC1_$SET_VT(uint16_t pid, int16_t value);

/*
 * PROC1_$VT_INT - Virtual timer interrupt handler
 * Original address: 0x00e1491e
 */
void PROC1_$VT_INT(void);

/*
 * PROC1_$SET_TS - Set timeslice value
 * Original address: 0x00e14a08
 */
void PROC1_$SET_TS(proc1_t *pcb, int16_t value);

/*
 * PROC1_$TS_END_CALLBACK - Timeslice end callback
 * Original address: 0x00e14a70
 */
void PROC1_$TS_END_CALLBACK(void);

/*
 * PROC1_$INIT_TS_TIMER - Initialize timeslice timer for process
 * Original address: 0x00e14b12
 */
void PROC1_$INIT_TS_TIMER(uint16_t pid);

/*
 * ============================================================================
 * Information Functions
 * ============================================================================
 */

/*
 * PROC1_$GET_INFO - Get process information
 * Original address: 0x00e14f52
 */
void PROC1_$GET_INFO(uint16_t pid, void *info, status_$t *status_ret);

/*
 * PROC1_$GET_INFO_INT - Get process info (internal)
 * Original address: 0x00e20f12
 */
void PROC1_$GET_INFO_INT(void);

/*
 * PROC1_$GET_LIST - Get list of processes
 * Original address: 0x00e15362
 */
void PROC1_$GET_LIST(void *list, uint16_t *count);

/*
 * PROC1_$GET_USP - Get user stack pointer
 * Original address: 0x00e20f0c
 */
void *PROC1_$GET_USP(void);

/*
 * ============================================================================
 * Address Space Functions
 * ============================================================================
 */

/*
 * PROC1_$SET_ASID - Set address space ID for current process
 * Sets the ASID in the current PCB and installs it in the MMU.
 * Original address: 0x00e148f8
 */
void PROC1_$SET_ASID(uint16_t asid);

/*
 * ============================================================================
 * Interrupt Handling
 * ============================================================================
 */

/*
 * PROC1_$INT_ADVANCE - Advance interrupt handling
 * Original address: 0x00e208f6
 */
void PROC1_$INT_ADVANCE(void);

/*
 * PROC1_$INT_EXIT - Exit from interrupt
 * Original address: 0x00e208fe
 */
void PROC1_$INT_EXIT(void);

#endif /* PROC1_H */
