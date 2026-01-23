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
#include "proc2/proc2.h"

/*
 * Volume table layout
 *
 * Volume table base: 0xe7a1cc (DISK_VOLUME_BASE)
 * Each entry is 0x48 (72) bytes (see DISK_VOLUME_SIZE in disk.h).
 *
 * Offsets relative to entry start (vol_idx * 0x48):
 *   +0x00: UID high (uint32_t)
 *   +0x04: UID low (uint32_t)
 *   +0x08: LV data pointer (uint32_t) - 0 for physical volumes
 *   +0x0c: Disk address start (uint32_t)
 *   +0x10: Disk address end (uint32_t)
 *   +0x14: Mount state (uint16_t)
 *   +0x16: Mount process (int16_t)
 *   +0x18: Device unit (uint16_t)
 *   +0x1a: Reserved
 *   +0x1c: Volume info 1 (uint16_t)
 *   +0x1e: Volume info 2 (uint16_t)
 *   +0x20: PV label info (16 bytes)
 *   +0x30: Reserved
 */
#define DISK_VOLUME_BASE      ((uint8_t *)0x00e7a1cc)

/* Volume entry field offsets */
#define DISK_UID_HIGH_OFFSET      0x00
#define DISK_UID_LOW_OFFSET       0x04
#define DISK_LV_DATA_OFFSET       0x08
#define DISK_ADDR_START_OFFSET    0x0c
#define DISK_ADDR_END_OFFSET      0x10
#define DISK_MOUNT_STATE_OFFSET   0x14
#define DISK_MOUNT_PROC_OFFSET    0x16
#define DISK_DEVICE_UNIT_OFFSET   0x18
#define DISK_VOL_INFO1_OFFSET     0x1c
#define DISK_VOL_INFO2_OFFSET     0x1e
#define DISK_PVLABEL_OFFSET       0x20
#define DISK_SHIFT_LOG2_OFFSET    0x22

/* Mount states */
#define DISK_MOUNT_FREE      0
#define DISK_MOUNT_RESERVED  1
#define DISK_MOUNT_ASSIGNED  2
#define DISK_MOUNT_BUSY      3
#define DISK_MOUNT_MIRROR    4

/* Valid volume index mask (volumes 1-10) - bits 1-10 set */
#define VALID_VOL_MASK  0x7fe

/* Maximum logical volume index */
#define MAX_LV_INDEX  10

/* Number of volume table entries to scan (indices 1-6) */
#define VOL_TABLE_SCAN_COUNT  6

/* PV Label UID constant at 0xe1738c */
extern uint32_t PV_LABEL__UID[];

/*
 * Internal data structures
 */

/* Diagnostic mode flag - enables special diagnostic I/O operations */
extern int8_t DISK_$DIAG;

/* Mount lock - protects mount table operations */
extern ml_$exclusion_t MOUNT_LOCK;

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
 * DISK_$PV_MOUNT_INTERNAL - Internal physical volume mount
 *
 * Core implementation for PV_ASSIGN_N. Handles device initialization,
 * volume table setup, and multi-volume configurations.
 *
 * Parameters:
 *   mount_type     - 0=normal, 1=boot, 2=mount, 4=remount
 *   device_num     - Device number
 *   unit_hi        - Unit high byte
 *   unit_lo        - Unit low byte (device unit)
 *   vol_idx_ptr    - Output: volume index assigned
 *   num_blocks_ptr - I/O: number of blocks
 *   sec_per_track_ptr - I/O: sectors per track
 *   num_heads_ptr  - I/O: number of heads
 *   pvlabel_info   - I/O: PV label info (16 bytes)
 *   status         - Output: status code
 *
 * Returns:
 *   Volume index assigned
 *
 * Original address: 0x00e6c2bc
 */
int16_t DISK_$PV_MOUNT_INTERNAL(int16_t mount_type, int16_t device_num,
                                 uint16_t unit_hi, uint16_t unit_lo,
                                 uint16_t *vol_idx_ptr, uint32_t *num_blocks_ptr,
                                 uint16_t *sec_per_track_ptr, uint16_t *num_heads_ptr,
                                 void *pvlabel_info, status_$t *status);

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
