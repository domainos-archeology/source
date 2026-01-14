/*
 * ML - Mutual Exclusion Locks Module
 *
 * This module provides synchronization primitives for Domain/OS:
 * - Resource Locks: High-level locks that support blocking waits
 * - Spin Locks: Low-level interrupt-disabling locks
 * - Exclusion Locks: Semaphore-like exclusion regions
 *
 * Lock ordering is enforced: a process cannot acquire a lower-numbered
 * lock while holding a higher-numbered lock. This prevents deadlocks.
 *
 * Memory layout (m68k):
 *   - Lock bytes: 0xE20BC4 (32 bytes, one per lock)
 *   - Lock event lists: 0xE20BE4 (16 bytes per lock)
 */

#ifndef ML_H
#define ML_H

#include "base/base.h"
#include "ec/ec.h"

/*
 * Exclusion lock structure
 *
 * This is a semaphore-like structure that allows one thread to enter
 * an exclusion region at a time, with waiters blocking until release.
 *
 * Size: 0x12 bytes (18 bytes)
 */
typedef struct ml_$exclusion_t {
    int32_t     f1;         /* 0x00: Unknown field */
    void        *f2;        /* 0x04: Pointer (self-referential in list) */
    void        *f3;        /* 0x08: Pointer (self-referential in list) */
    int32_t     f4;         /* 0x0C: Event count value */
    int16_t     f5;         /* 0x10: State: -1 = unlocked, >= 0 = locked + waiter count */
} ml_$exclusion_t;

/*
 * Spin lock token type
 *
 * The token is used to restore the processor state (SR) after a spin lock
 * is released. On m68k, this holds the status register value.
 */
typedef uint16_t ml_$spin_token_t;

/*
 * Known lock IDs used throughout the kernel
 * (defined here for reference; actual definitions are in respective modules)
 */
#define ML_LOCK_PROC2       4       /* PROC2 lock (proc2/proc2.h) */
#define ML_LOCK_EC2         6       /* EC2 lock (ec/ec.h) */
#define ML_LOCK_PROC1       0x0B    /* Process creation lock (proc1/proc1.h) */
#define ML_LOCK_MST_ASID    0x0C    /* MST ASID allocation lock */
#define ML_LOCK_CAL         0x0E    /* Calendar lock */
#define ML_LOCK_DISK        0x0F    /* Disk lock */
#define ML_LOCK_AST         0x12    /* AST lock */
#define ML_LOCK_PMAP        0x14    /* PMAP lock */
#define ML_LOCK_MST_MMU     0x14    /* MST MMU lock */

/*
 * Global variables (m68k-specific addresses)
 */
#if defined(M68K)
    /* Lock byte array: bit 0 indicates lock is held */
    #define ML_$LOCK_BYTES      ((volatile uint8_t *)0xE20BC4)
    /* Lock event lists: each entry is 16 bytes (ec + wait count) */
    #define ML_$LOCK_EVENTS     ((ec_$eventcount_t *)0xE20BE4)
#else
    extern volatile uint8_t ML_$LOCK_BYTES[];
    extern ec_$eventcount_t ML_$LOCK_EVENTS[];
#endif

/*
 * ============================================================================
 * Resource Lock Functions
 * ============================================================================
 *
 * Resource locks are high-level locks that support blocking waits.
 * They use event counts to allow processes to sleep while waiting.
 *
 * Lock ordering: The kernel enforces that locks must be acquired in
 * ascending order of lock ID. Attempting to acquire a lock with a lower
 * ID than any currently held lock will panic.
 */

/*
 * ML_$LOCK - Acquire a resource lock
 *
 * Acquires the specified resource lock, blocking if necessary until
 * the lock becomes available. Updates the current process's lock state.
 *
 * Parameters:
 *   resource_id - The lock ID to acquire (0-31)
 *
 * Panics if:
 *   - Attempting to acquire a lower-numbered lock than already held
 *
 * Original address: 0x00E20B12
 */
void ML_$LOCK(int16_t resource_id);

/*
 * ML_$UNLOCK - Release a resource lock
 *
 * Releases the specified resource lock and wakes any waiting processes.
 * May trigger rescheduling if a higher-priority process was waiting.
 *
 * Parameters:
 *   resource_id - The lock ID to release (0-31)
 *
 * Panics if:
 *   - The lock is not currently held by this process
 *
 * Original address: 0x00E20B62
 */
void ML_$UNLOCK(int16_t resource_id);

/*
 * ============================================================================
 * Spin Lock Functions
 * ============================================================================
 *
 * Spin locks are low-level locks that disable interrupts while held.
 * They should be held for very short durations only.
 *
 * On SAU2 (single-processor), these simply disable/restore interrupts
 * and don't actually use the lock parameter.
 */

/*
 * ML_$SPIN_LOCK - Acquire a spin lock
 *
 * Acquires the specified spin lock by disabling interrupts.
 * Returns a token that must be passed to ML_$SPIN_UNLOCK to restore state.
 *
 * Parameters:
 *   lockp - Pointer to the lock variable (unused on SAU2)
 *
 * Returns:
 *   Token containing saved processor state (SR on m68k)
 *
 * Original address: 0x00E20BB6
 */
ml_$spin_token_t ML_$SPIN_LOCK(void *lockp);

/*
 * ML_$SPIN_UNLOCK - Release a spin lock
 *
 * Releases the specified spin lock and restores the processor state.
 *
 * Parameters:
 *   lockp - Pointer to the lock variable (unused on SAU2)
 *   token - Token returned by ML_$SPIN_LOCK
 *
 * Original address: 0x00E20BBE
 */
void ML_$SPIN_UNLOCK(void *lockp, ml_$spin_token_t token);

/*
 * ============================================================================
 * Exclusion Lock Functions
 * ============================================================================
 *
 * Exclusion locks provide semaphore-like functionality with support for
 * blocking waits. Unlike resource locks, they can be used for arbitrary
 * critical sections without lock ordering constraints.
 */

/*
 * ML_$EXCLUSION_INIT - Initialize an exclusion lock
 *
 * Initializes an exclusion lock structure to the unlocked state.
 * Must be called before using the exclusion lock.
 *
 * Parameters:
 *   excl - Pointer to the exclusion lock to initialize
 *
 * Original address: 0x00E20E34
 */
void ML_$EXCLUSION_INIT(ml_$exclusion_t *excl);

/*
 * ML_$EXCLUSION_START - Enter an exclusion region
 *
 * Attempts to enter an exclusion region, blocking if another process
 * is already inside. The caller's inhibit count is incremented.
 *
 * Parameters:
 *   excl - Pointer to the exclusion lock
 *
 * Original address: 0x00E20DF8
 */
void ML_$EXCLUSION_START(ml_$exclusion_t *excl);

/*
 * ML_$EXCLUSION_STOP - Leave an exclusion region
 *
 * Leaves an exclusion region, waking any waiting processes.
 * May trigger rescheduling.
 *
 * Parameters:
 *   excl - Pointer to the exclusion lock
 *
 * Original address: 0x00E20E7E
 */
void ML_$EXCLUSION_STOP(ml_$exclusion_t *excl);

/*
 * ML_$EXCLUSION_CHECK - Check if exclusion region is locked
 *
 * Tests whether an exclusion lock is currently held.
 *
 * Parameters:
 *   excl - Pointer to the exclusion lock
 *
 * Returns:
 *   Non-zero if the exclusion region is occupied
 *   0 if unlocked
 *
 * Original address: 0x00E20E4A
 */
int8_t ML_$EXCLUSION_CHECK(ml_$exclusion_t *excl);

/*
 * ML_$COND_EXCLUSION_START - Conditionally enter an exclusion region
 *
 * Attempts to enter an exclusion region without blocking.
 *
 * Parameters:
 *   excl - Pointer to the exclusion lock
 *
 * Returns:
 *   Non-zero if successfully entered the exclusion region
 *   0 if the region was already occupied
 *
 * Original address: 0x00E20E56
 */
int8_t ML_$COND_EXCLUSION_START(ml_$exclusion_t *excl);

/*
 * ML_$COND_EXCLUSION_STOP - Leave a conditionally entered exclusion region
 *
 * Leaves an exclusion region that was entered via ML_$COND_EXCLUSION_START.
 * This is a simpler version that doesn't wake waiters.
 *
 * Parameters:
 *   excl - Pointer to the exclusion lock
 *
 * Original address: 0x00E20E74
 */
void ML_$COND_EXCLUSION_STOP(ml_$exclusion_t *excl);

#endif /* ML_H */
