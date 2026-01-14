/*
 * DISK - Disk Subsystem Interface
 *
 * This module provides the disk subsystem interface for Domain/OS.
 * It implements device registration, I/O queuing, and buffer management,
 * delegating to device-specific drivers via jump tables.
 *
 * The disk subsystem maintains:
 * - Volume table with mount state and device info
 * - Device registration table for driver callbacks
 * - I/O queues for asynchronous operations
 * - Buffer cache integration via DBUF module
 */

#ifndef DISK_H
#define DISK_H

#include "base/base.h"
#include "ec/ec.h"
#include "ml/ml.h"

/*
 * Maximum number of volumes and devices
 */
#define DISK_MAX_VOLUMES 64 /* 0x40 */
#define DISK_MAX_DEVICES 32 /* 0x20 */

/*
 * Volume entry size
 */
#define DISK_VOLUME_SIZE 72 /* 0x48 bytes per volume */

/*
 * Device registration entry size
 */
#define DISK_DEVICE_SIZE 12 /* 0x0c bytes per device */

/*
 * Mount states
 */
#define DISK_MOUNT_UNMOUNTED 0
#define DISK_MOUNT_MOUNTED 3

/*
 * Disk lock ID
 */
#define DISK_LOCK_ID 15 /* 0x0f */

/*
 * Status codes
 */
#define status_$disk_write_protected 0x00080007
#define status_$volume_not_properly_mounted 0x0008000d
#define status_$invalid_volume_index 0x0008000f
#define status_$disk_illegal_request_for_device 0x0008002a

/*
 * Volume entry structure (partial - 72 bytes per entry)
 * Base address: 0xe7a1cc
 *
 * Layout relative to base + (vol_idx * 0x48):
 *   +0x00: Event counter (disk EC)
 *   +0x90: Mount state (word)
 *   +0xa5: Write protect flags (byte)
 */
typedef struct {
  uint8_t ec_data[16];      /* +0x00: Event counter data */
  uint8_t _reserved1[0x80]; /* +0x10: Reserved */
  uint16_t mount_state;     /* +0x90: Mount state */
  uint8_t _reserved2[0x13]; /* +0x92: Reserved */
  uint8_t write_protect;    /* +0xa5: Write protect flags */
  uint8_t _reserved3[0x02]; /* +0xa6: Padding to 0x48 boundary */
  /* Note: actual structure extends through event counters at +0x378, +0x384 */
} disk_volume_entry_t;

/*
 * Device registration entry structure (12 bytes per entry)
 * Base address: 0xe7ad5c
 *
 * Layout:
 *   +0x00: Jump table pointer (long)
 *   +0x04: Device type (word)
 *   +0x06: Controller number (word)
 *   +0x08: Unit count (word)
 *   +0x0a: Flags (word)
 */
typedef struct {
  void *jump_table;     /* +0x00: Pointer to device operations */
  uint16_t device_type; /* +0x04: Device type identifier */
  uint16_t controller;  /* +0x06: Controller number */
  uint16_t unit_count;  /* +0x08: Number of units */
  uint16_t flags;       /* +0x0a: Device flags */
} disk_device_entry_t;

/*
 * Device jump table structure
 *
 * Layout:
 *   +0x00: (reserved)
 *   +0x04: (reserved)
 *   +0x08: DINIT - Device initialization
 *   +0x0c: (reserved)
 *   +0x10: DO_IO - Perform I/O operation
 */
typedef struct {
  void *_reserved1; /* +0x00 */
  void *_reserved2; /* +0x04 */
  void *dinit;      /* +0x08: Device init function */
  void *_reserved3; /* +0x0c */
  void *do_io;      /* +0x10: I/O function */
} disk_jump_table_t;

/*
 * Global data areas
 */

/* Disk subsystem base at 0xe7a1cc */
extern uint8_t DISK_$DATA[];

/* Volume table (64 entries, 72 bytes each) */
extern disk_volume_entry_t DISK_$VOLUMES[];

/* Device registration table at 0xe7ad5c (32 entries, 12 bytes each) */
extern disk_device_entry_t DISK_$DEVICES[];

/* Event counter for disk operations */
extern void *DISK_$EC;

/* Exclusion locks */
extern void *DISK_$EXCLUSION_1; /* +0x90 */
extern void *DISK_$EXCLUSION_2; /* +0xa8 */

/*
 * Function prototypes - Public API
 */

/* Initialization */
void DISK_$INIT(void);

/* Buffer operations */
void *DISK_$GET_BLOCK(int16_t vol_idx, int32_t daddr, void *expected_uid,
                      uint16_t param_4, uint16_t param_5, status_$t *status);
void DISK_$SET_BUFF(void *buffer, uint16_t flags, void *param_3);
void DISK_$INVALIDATE(uint16_t vol_idx);

/* Queue operations */
void DISK_$INIT_QUE(void *queue);
void DISK_$ADD_QUE(uint16_t flags, void *dev_entry, void *queue,
                   void *req_list);
void DISK_$WAIT_QUE(void *queue, status_$t *status);
void DISK_$ERROR_QUE(void *req, uint16_t param_2, void *param_3);
void *DISK_$GET_QBLKS(int16_t vol_idx, int16_t count, status_$t *status);
void DISK_$RTN_QBLKS(void *blocks);
void DISK_$SORT(void *dev_entry, void **queue_ptr);

/* I/O operations */
void DISK_$READ(int16_t vol_idx, void *buffer, void *daddr, void *count,
                status_$t *status);
void DISK_$WRITE(int16_t vol_idx, void *buffer, void *daddr, void *count,
                 status_$t *status);
void DISK_$READ_MULTI(int16_t vol_idx, void *req_list, void *param_3,
                      status_$t *status);
void DISK_$WRITE_MULTI(int16_t vol_idx, void *req_list, void *param_3,
                       status_$t *status);
void DISK_$DO_IO(void *dev_entry, void *req, void *param_3, void *result);

/* Allocation operations */
void DISK_$ALLOC_W_HINT(uint16_t vol, uint32_t hint, uint32_t *block,
                        uint32_t count, status_$t *status);

/* Format operations */
void DISK_$FORMAT(uint16_t *vol_idx_ptr, uint16_t *cyl_ptr, uint16_t *head_ptr,
                  status_$t *status);
void DISK_$FORMAT_WHOLE(uint16_t *vol_idx_ptr, status_$t *status);

/* Device management */
uint8_t DISK_$REGISTER(uint16_t *type, uint16_t *controller, uint16_t *units,
                       uint16_t *flags, void **jump_table);
void *DISK_$GET_DRTE(int16_t index);
void DISK_$MNT_DINIT(uint16_t vol_idx, void **dev_ptr, void *param_3,
                     void *param_4, void *param_5, void *param_6,
                     void *param_7);
void DISK_$SHUTDOWN(int16_t vol_idx, status_$t *status);
void DISK_$SPIN_DOWN(int16_t vol_idx, status_$t *status);
void DISK_$REVALID(int16_t vol_idx);
void DISK_$WRITE_PROTECT(int16_t mode, int16_t vol_idx, status_$t *status);
void DISK_$GET_STATS(int16_t vol_idx, void *stats, status_$t *status);
void DISK_$UNASSIGN(uint16_t *vol_idx_ptr, status_$t *status);
void DISK_$UNASSIGN_ALL(void);
void DISK_$REVALIDATE(int16_t vol_idx);
void DISK_$DISMOUNT(uint16_t vol_idx);
void DISK_$GET_ERROR_INFO(void *buffer);
void DISK_$LVUID_TO_VOLX(void *uid_ptr, int16_t *vol_idx, status_$t *status);

/* Async I/O operations */
void DISK_$AS_READ(uint16_t *vol_idx_ptr, uint32_t *daddr_ptr,
                   uint16_t *count_ptr, uint32_t *info, status_$t *status);
void DISK_$AS_WRITE(uint16_t *vol_idx_ptr, uint32_t *daddr_ptr, uint32_t buffer,
                    uint32_t *info, status_$t *status);
void DISK_$AS_XFER_MULTI(uint16_t *vol_idx_ptr, int16_t *count_ptr,
                         int16_t *op_type_ptr, uint32_t *daddr_array,
                         uint32_t **info_array, uint32_t *buffer_array,
                         uint32_t *status_array, status_$t *status);
void DISK_$AS_OPTIONS(uint16_t *vol_idx_ptr, uint16_t *options_ptr,
                      status_$t *status);

/* Diagnostic and manufacturing operations */
void DISK_$DIAG_IO(int16_t *op_ptr, uint16_t *vol_idx_ptr, uint32_t *daddr_ptr,
                   void *buffer, uint32_t *info, status_$t *status);
void DISK_$READ_MFG_BADSPOTS(uint16_t *vol_idx_ptr, uint32_t *buffer_ptr,
                             uint32_t count, status_$t *status);
void DISK_$GET_MNT_INFO(uint16_t *vol_idx_ptr, void *param_2, void *info,
                        status_$t *status);

/*
 * External functions used by DISK
 */
/* DBUF functions */
extern void *DBUF_$GET_BLOCK(int16_t vol_idx, int32_t daddr, void *uid,
                             uint16_t p4, uint16_t p5, status_$t *status);
extern void DBUF_$SET_BUFF(void *buffer, uint16_t flags, void *param_3);
extern void DBUF_$INVALIDATE(int32_t param_1, uint16_t vol_idx);

/* Internal I/O function */
extern status_$t DISK_IO(int16_t op, int16_t vol_idx, void *daddr, void *buffer,
                         void *count);

#endif /* DISK_H */
