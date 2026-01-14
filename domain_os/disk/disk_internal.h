/*
 * DISK Internal - Disk Subsystem Internal Definitions
 *
 * This header contains internal definitions used within the disk subsystem.
 * External code should use disk.h instead.
 */

#ifndef DISK_INTERNAL_H
#define DISK_INTERNAL_H

#include "disk/disk.h"
#include "ml/ml.h"
#include "proc1/proc1.h"

/*
 * Internal data structures
 */

/* Diagnostic mode flag - enables special diagnostic I/O operations */
extern int8_t DISK_$DIAG;

/* Mount lock - protects mount table operations */
extern void *MOUNT_LOCK;

/* Exclusion locks for disk operations (at specific offsets from DISK_$DATA) */
extern ml_$exclusion_t ml_$exclusion_t_00e7a274;  /* DISK_$DATA +0xa8 */
extern ml_$exclusion_t ml_$exclusion_t_00e7a25c;  /* DISK_$DATA +0x90 */

/*
 * Error status variables
 *
 * These are pre-defined status codes used for CRASH_SYSTEM calls.
 */
extern void *Disk_Queued_Drivers_Not_Supported_Err;
extern void *Disk_Driver_Logic_Err;

/*
 * Internal helper functions (unidentified - FUN_*)
 *
 * These functions need to be identified and renamed.
 */

/*
 * FUN_00e3be8a - Get disk queue blocks
 *
 * Internal function for allocating queue blocks for I/O operations.
 *
 * Parameters:
 *   vol_idx - Volume index
 *   mode    - Allocation mode
 *   count   - Pointer to count (input/output)
 *   status  - Receives status code
 *
 * Returns:
 *   Pointer to allocated blocks
 *
 * Original address: 0x00e3be8a
 */
void *FUN_00e3be8a(int16_t vol_idx, int16_t mode, void *count, status_$t *status);

/*
 * FUN_00e3c01a - Return disk queue blocks
 *
 * Internal function to return previously allocated queue blocks.
 *
 * Parameters:
 *   vol_idx - Volume index
 *   blocks  - Pointer to blocks to return
 *   param_3 - Additional parameter
 *
 * Original address: 0x00e3c01a
 */
void FUN_00e3c01a(int16_t vol_idx, void *blocks, void *param_3);

/*
 * FUN_00e3c9fe - Wait for disk queue completion
 *
 * Internal function to wait for queued I/O operations to complete.
 *
 * Parameters:
 *   mask     - Wait mask
 *   counter1 - Event counter 1
 *   counter2 - Event counter 2
 *
 * Original address: 0x00e3c9fe
 */
void FUN_00e3c9fe(uint16_t mask, void *counter1, void *counter2);

/*
 * AS_IO_SETUP - Setup for async I/O operations
 *
 * Prepares a buffer for asynchronous I/O by wiring it in memory.
 *
 * Parameters:
 *   vol_idx_ptr - Pointer to volume index
 *   buffer      - Buffer address
 *   status      - Receives status code
 *
 * Returns:
 *   Wired buffer address for I/O
 *
 * Original address: TBD
 */
uint32_t AS_IO_SETUP(uint16_t *vol_idx_ptr, uint32_t buffer, status_$t *status);

#endif /* DISK_INTERNAL_H */
