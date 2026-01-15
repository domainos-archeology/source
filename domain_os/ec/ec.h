/*
 * EC - Event Count Module
 *
 * This module provides event count synchronization primitives for Domain/OS.
 * There are two levels of event counts:
 *
 * Level 1 (EC_$): Direct pointer-based eventcounts
 *   - Used for kernel-internal synchronization
 *   - Synchronization via interrupt disable (ori #0x700,SR / andi #-0x701,SR)
 *   - 12-byte structure with value and waiter list
 *
 * Level 2 (EC2_$): Index-based eventcounts
 *   - Used for user-level synchronization
 *   - Synchronization via lock (ec2_$lock = 6)
 *   - Indices 0-0x3E8 map to registered EC1s or PBU pool
 *   - Indices > 0x3E8 are direct pointers
 *
 * Memory layout (m68k):
 *   - EC1 array base: 0xE20F60 (64 entries, 12 bytes each)
 *   - EC2 waiter table: 0xE7C06C (225 entries, 12 bytes each)
 *   - EC2 PBU (Per-Bus Unit) ECs: 0xE88460 (32 entries, 24 bytes each)
 */

#ifndef EC_H
#define EC_H

#include "base/base.h"

/*
 * Level 1 Event Count waiter structure
 * Waiters are linked in a list from the eventcount
 * Size: 16 bytes (0x10)
 */
typedef struct ec_$eventcount_waiter_t {
    int32_t     wait_val;           /* 0x00: Value waiting for */
    struct ec_$eventcount_waiter_t *prev_waiter;  /* 0x04: Previous waiter */
    struct ec_$eventcount_waiter_t *next_waiter;  /* 0x08: Next waiter */
    void        *pcb;               /* 0x0C: Process control block */
} ec_$eventcount_waiter_t;

/*
 * Level 1 Event Count structure
 * Size: 12 bytes (0x0C)
 */
typedef struct ec_$eventcount_t {
    int32_t     value;              /* 0x00: Current value */
    ec_$eventcount_waiter_t *waiter_list_head;  /* 0x04: Head of waiter list */
    ec_$eventcount_waiter_t *waiter_list_tail;  /* 0x08: Tail of waiter list */
} ec_$eventcount_t;

/*
 * Level 2 Event Count structure
 * Size: 6 bytes
 */
typedef struct ec2_$eventcount_t {
    int32_t     value;              /* 0x00: Current value or EC1 pointer/index */
    int16_t     awaiters;           /* 0x04: Count of waiters or waiter index */
} ec2_$eventcount_t;

/*
 * EC2 waiter entry structure
 * Size: 12 bytes (0x0C)
 * Located in waiter table at 0xE7C06C
 */
typedef struct ec2_waiter_t {
    int32_t     wait_val;           /* 0x00: Value waiting for */
    int16_t     next;               /* 0x04: Next waiter index */
    int16_t     prev;               /* 0x06: Previous waiter index */
    int16_t     proc_id;            /* 0x08: Process ID */
    int16_t     pad;                /* 0x0A: Padding */
} ec2_waiter_t;

/*
 * Lock IDs
 */
#define EC2_LOCK_ID                 6       /* ec2_$lock */

/*
 * Status codes
 */
#define status_$ec2_bad_event_count                     0x00180004
#define status_$ec2_async_fault_while_waiting           0x00180003
#define status_$ec2_unable_to_allocate_level_1_eventcount  0x00180005
#define status_$ec2_level_1_ec_not_allocated            0x00180006
#define status_$cleanup_handler_set                     0x00120035
#define status_$fault_protection_boundary_violation     0x0012000B

/*
 * Memory addresses (m68k)
 */
extern ec_$eventcount_t EC1_ARRAY_BASE[];
extern ec2_waiter_t EC2_WAITER_TABLE_BASE[];
extern ec2_$eventcount_t EC2_PBU_ECS_BASE[];

/*
 * External references (public memory-mapped data)
 */
extern ec2_$eventcount_t EC2_$PBU_ECS;      /* PBU eventcount pool */

/*
 * Note: EC source files that need PROC1_$ functions/globals should
 * include "proc1/proc1.h" directly. This avoids circular dependencies
 * since proc1.h already includes ec.h.
 *
 * Note: Internal EC2 data (allocation bitmaps, free lists, etc.) are
 * declared in ec_internal.h. FIM functions are in fim/fim.h.
 */

/*
 * ============================================================================
 * Level 1 Event Count Functions (EC_$)
 * ============================================================================
 */

/*
 * EC_$INIT - Initialize an event count
 *
 * Sets value to 0 and initializes empty waiter list.
 *
 * Parameters:
 *   ec - Pointer to eventcount structure
 *
 * Original address: 0x00e151fe
 */
void EC_$INIT(ec_$eventcount_t *ec);

/*
 * EC_$READ - Read current eventcount value
 *
 * Parameters:
 *   ec - Pointer to eventcount structure
 *
 * Returns:
 *   Current value of the eventcount
 *
 * Original address: 0x00e15214
 */
int32_t EC_$READ(ec_$eventcount_t *ec);

/*
 * EC_$WAIT - Wait for eventcount(s) to reach value(s)
 *
 * Waits on up to 3 eventcounts. Array is NULL-terminated if
 * fewer than 3.
 *
 * Parameters:
 *   ecs - Array of up to 3 eventcount pointers
 *   wait_val - Array of wait values
 *
 * Returns:
 *   Index of satisfied eventcount minus 1
 *
 * Original address: 0x00e20610
 */
int16_t EC_$WAIT(ec_$eventcount_t *ecs[3], int32_t *wait_val);

/*
 * EC_$WAITN - Wait for N eventcounts
 *
 * Parameters:
 *   ecs - Array of eventcount pointers
 *   wait_val - Array of wait values
 *   num_ecs - Number of eventcounts to wait on
 *
 * Returns:
 *   Index of satisfied eventcount
 *
 * Original address: 0x00e2063e
 */
uint16_t EC_$WAITN(ec_$eventcount_t **ecs, int32_t *wait_val, int16_t num_ecs);

/*
 * EC_$ADVANCE - Advance eventcount and dispatch
 *
 * Increments eventcount, wakes eligible waiters, and calls dispatcher.
 * Uses interrupt disable for synchronization.
 *
 * Parameters:
 *   ec - Pointer to eventcount structure
 *
 * Original address: 0x00e206ee
 */
void EC_$ADVANCE(ec_$eventcount_t *ec);

/*
 * EC_$ADVANCE_ALL - Wake all waiters on eventcount
 *
 * Sets value to 0x7FFFFFFF to wake all waiters.
 * Uses interrupt disable for synchronization.
 *
 * Parameters:
 *   ec - Pointer to eventcount structure
 *
 * Original address: 0x00e20702
 */
void EC_$ADVANCE_ALL(ec_$eventcount_t *ec);

/*
 * EC_$ADVANCE_WITHOUT_DISPATCH - Advance without calling dispatcher
 *
 * Like EC_$ADVANCE but does not call PROC1_$DISPATCH_INT.
 * Saves and restores SR for synchronization.
 *
 * Parameters:
 *   ec - Pointer to eventcount structure
 *
 * Original address: 0x00e20718
 */
void EC_$ADVANCE_WITHOUT_DISPATCH(ec_$eventcount_t *ec);

/*
 * Internal helper: ADVANCE_INT
 * Increments value and wakes eligible waiters.
 * Called with interrupts disabled.
 *
 * Original address: 0x00e2072c
 */
void ADVANCE_INT(ec_$eventcount_t *ec);

/*
 * Internal helper: ADVANCE_ALL (implementation)
 * Sets value to MAX_INT and processes waiter list.
 *
 * Original address: 0x00e207c6
 */
void ADVANCE_ALL_INT(ec_$eventcount_t *ec);

/*
 * ============================================================================
 * Level 2 Event Count Functions (EC2_$)
 * ============================================================================
 */

/*
 * EC2_$INIT_S - System initialization for EC2 subsystem
 *
 * Initializes EC1 array, waiter table, and PBU pool.
 *
 * Original address: 0x00e30970
 */
void EC2_$INIT_S(void);

/*
 * EC2_$INIT - Initialize an EC2
 *
 * Only initializes if ec > 0x3E8 (direct pointer).
 *
 * Parameters:
 *   ec - EC2 pointer (must be > 0x3E8)
 *
 * Original address: 0x00e42c60
 */
void EC2_$INIT(ec2_$eventcount_t *ec);

/*
 * EC2_$READ - Read EC2 value
 *
 * Parameters:
 *   ec - EC2 pointer or index
 *
 * Returns:
 *   Current value
 *
 * Original address: 0x00e42c7c
 */
int32_t EC2_$READ(ec2_$eventcount_t *ec);

/*
 * EC2_$ADVANCE - Advance EC2 and wake waiters
 *
 * Parameters:
 *   ec - EC2 pointer
 *   status_ret - Status return
 *
 * Original address: 0x00e42cae
 */
void EC2_$ADVANCE(ec2_$eventcount_t *ec, status_$t *status_ret);

/*
 * EC2_$WAIT - Wait on multiple EC2s
 *
 * Parameters:
 *   ec - Array of EC2 pointers
 *   wait_vals - Array of wait values
 *   count - Pointer to count of ECs
 *   status_ret - Status return
 *
 * Returns:
 *   Index of satisfied EC2
 *
 * Original address: 0x00e42358
 */
int16_t EC2_$WAIT(ec2_$eventcount_t *ec, int32_t *wait_vals,
                  int16_t *count, status_$t *status_ret);

/*
 * EC2_$WAKEUP - Wake all waiters on EC2
 *
 * Parameters:
 *   ec - EC2 pointer
 *   status_ret - Status return
 *
 * Original address: 0x00e4285a
 */
void EC2_$WAKEUP(ec2_$eventcount_t *ec, status_$t *status_ret);

/*
 * EC2_$REGISTER_EC1 - Register an EC1 for EC2 access
 *
 * Parameters:
 *   ec1 - EC1 pointer to register
 *   status_ret - Status return
 *
 * Returns:
 *   EC2 index for the registered EC1
 *
 * Original address: 0x00e4293c
 */
void *EC2_$REGISTER_EC1(ec_$eventcount_t *ec1, status_$t *status_ret);

/*
 * EC2_$ALLOCATE_EC1 - Allocate a new EC1 from PBU pool
 *
 * Parameters:
 *   status_ret - Status return
 *
 * Returns:
 *   EC2 index (0x101-0x120) for allocated EC1
 *
 * Original address: 0x00e429da
 */
void *EC2_$ALLOCATE_EC1(status_$t *status_ret);

/*
 * EC2_$GET_EC1_ADDR - Get address of EC1 from EC2 index
 *
 * Parameters:
 *   ec - EC2 index
 *   status_ret - Status return
 *
 * Returns:
 *   Pointer to EC1 structure
 *
 * Original address: 0x00e42a8a
 */
ec_$eventcount_t *EC2_$GET_EC1_ADDR(ec2_$eventcount_t *ec, status_$t *status_ret);

/*
 * EC2_$RELEASE_EC1 - Release an allocated EC1
 *
 * Parameters:
 *   ec - EC2 index of EC1 to release
 *   status_ret - Status return
 *
 * Original address: 0x00e42b32
 */
void EC2_$RELEASE_EC1(ec2_$eventcount_t *ec, status_$t *status_ret);

/*
 * EC2_$GET_VAL - Get value from EC2
 *
 * Parameters:
 *   ec - EC2 pointer/index
 *   status_ret - Status return
 *
 * Returns:
 *   Current value (0x7FFFFFFF on error)
 *
 * Original address: 0x00e42be0
 */
int32_t EC2_$GET_VAL(ec2_$eventcount_t *ec, status_$t *status_ret);

#endif /* EC_H */
