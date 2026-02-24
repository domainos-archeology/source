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
#include "dbuf/dbuf.h"
#include "time/time.h"

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
extern uid_t PV_LABEL_$UID;

/* LV Label UID constant at 0xe17394 */
extern uid_t LV_LABEL_$UID;

/*
 * Disk module data layout (A5-relative offsets)
 *
 * The disk subsystem uses a Pascal module data area at DISK_$DATA (0xe7a1cc).
 * Internal functions access fields through byte offsets from this base.
 * In the original m68k code, A5 holds the base pointer.
 */
#define DMOD_EVENTCOUNT       0x000  /* ec_$eventcount_t - module eventcount */
#define DMOD_REQ_QUEUE        0x00E  /* int16_t[64] - circular request buffer (1-indexed) */
#define DMOD_EXCLUSION        0x090  /* ml_$exclusion_t - module exclusion lock */
#define DMOD_RESERVE_BLOCK    0x0BC  /* void* - reserve block for write-mode allocation */
#define DMOD_FREE_HEAD        0x0C0  /* void* - free list head */
#define DMOD_PAGES_ALLOC      0xAF0  /* int16_t - pool pages allocated */
#define DMOD_REQ_READ_IDX     0xAF2  /* int16_t - request queue read index */
#define DMOD_REQ_WRITE_IDX    0xAF4  /* int16_t - request queue write index */
#define DMOD_PENDING_COUNT    0xAF6  /* int16_t - pending request count */
#define DMOD_AVAIL_COUNT      0xAF8  /* int16_t - available block count */
#define DMOD_ALLOC_DISABLED   0xAFA  /* int8_t - pool growth disabled (0xFF=disabled) */
#define DMOD_RESERVE_AVAIL    0xAFC  /* int8_t - reserve block available (0xFF=available) */

/* Request queue size (entries 1..64, circular) */
#define DMOD_REQ_QUEUE_SIZE   0x40

/*
 * Queue block field offsets
 *
 * Queue blocks (disk I/O request blocks) are linked in free and allocated
 * chains. These offsets are used for initialization during allocation.
 */
#define DISK_QBLK_FORWARD     0x00  /* void* - next in allocated chain */
#define DISK_QBLK_FREE_NEXT   0x08  /* void* - next in free list */
#define DISK_QBLK_STATUS      0x0C  /* uint32_t - I/O status */
#define DISK_QBLK_FLAGS       0x1C  /* uint16_t - I/O flags */
#define DISK_QBLK_OWNER       0x1E  /* uint8_t - owning process ID */
#define DISK_QBLK_RESERVED    0x1F  /* uint8_t - reserved */

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

/* I/O request structure (used internally) */
typedef struct disk_io_req_t {
    uint32_t reserved1;         /* +0x00 */
    uint32_t daddr;             /* +0x04: Disk address */
    uint8_t head;               /* +0x06: Head number (for format) */
    uint8_t sector;             /* +0x07: Sector number (for format) */
    uint32_t reserved2;         /* +0x08 */
    status_$t status;           /* +0x0C: Result status */
    uint32_t reserved3;         /* +0x10 */
    uint32_t ppn;               /* +0x14: Physical page number */
    uint16_t reserved4;         /* +0x18 */
    uint16_t count;             /* +0x1A: Transfer count */
    uint8_t reserved5;          /* +0x1C */
    uint8_t reserved6;          /* +0x1D */
    uint8_t reserved7;          /* +0x1E */
    uint8_t flags;              /* +0x1F: Request flags */
    uint32_t header[8];         /* +0x20: Block header data */
    uint32_t timestamp;         /* +0x2C: Timestamp (for writes) */
    uint32_t reserved8[2];      /* +0x30, +0x34 */
    uint16_t checksum;          /* +0x3A: Checksum */
} disk_io_req_t;

extern status_$t DISK_IO(uint16_t op, uint16_t vol_idx, uint32_t daddr,
                         uint32_t ppn, int32_t *info);


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
 * disk_$grow_qblk_pool - Grow the disk queue block pool
 *
 * Allocates physical pages and initializes new queue blocks in the
 * disk module's free pool. Called under the module exclusion lock
 * when allocation requests cannot be satisfied from the current pool.
 *
 * Parameters:
 *   count - Requested number of blocks (used to calculate pages needed)
 *
 * Original address: 0x00e3bc40
 * Size: 586 bytes
 */
void disk_$grow_qblk_pool(int16_t count);

/*
 * disk_$get_qblks_internal - Internal queue block allocation body
 *
 * Pascal module body for DISK_$GET_QBLKS. Allocates queue blocks
 * from the disk pool using ML_$EXCLUSION for synchronization.
 * Waits via EC_$WAIT if blocks are unavailable.
 *
 * Uses A5 register as Pascal module data pointer (disk module data).
 *
 * Parameters:
 *   count     - Number of queue blocks to allocate
 *   mode      - Negative for write mode, non-negative for read
 *   first_out - Output: pointer to head of allocated block list
 *   last_out  - Output: pointer to tail of allocated block list
 *
 * Original address: 0x00e3be8a
 */
void disk_$get_qblks_internal(int16_t count, int8_t mode, void *first_out, void *last_out);

/*
 * disk_$rtn_qblks_internal - Return disk queue blocks
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
void disk_$rtn_qblks_internal(int16_t vol_idx, void *blocks, void *param_3);

/*
 * disk_$wait_io - Wait for disk I/O completion
 *
 * Waits on eventcounts for queued I/O operations to complete.
 * Uses EC_$WAIT with 3 eventcounts (two per-process disk ECs plus
 * TIME_$CLOCKH). Iterates 10 disk entries (0x48 spacing) checking
 * DISK_$ERROR_QUE for matching bits in the wait mask.
 *
 * Parameters:
 *   mask     - Bitmask of volumes to wait on
 *   counter1 - Event counter pointer 1
 *   counter2 - Event counter pointer 2
 *
 * Original address: 0x00E3C9FE
 * Size: 188 bytes
 */
void disk_$wait_io(uint16_t mask, void *counter1, void *counter2);

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
