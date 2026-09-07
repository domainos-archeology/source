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
#define status_$volume_in_use 0x0008000b
#define status_$volume_table_full 0x0008000c
#define status_$volume_not_properly_mounted 0x0008000d
#define status_$operation_requires_a_physical_volume 0x0008000e
#define status_$invalid_volume_index 0x0008000f
#define status_$invalid_logical_volume_index 0x00080014
#define status_$invalid_unit_number 0x00080018
#define status_$invalid_physical_volume_label 0x0008001a
#define status_$storage_module_stopped 0x0008001b
#define status_$disk_illegal_request_for_device 0x0008002a
#define status_$disk_is_full 0x00010002
#define status_$disk_needs_salvaging 0x00010005

/*
 * The per-volume descriptor is internal to the disk subsystem: see
 * disk_$volume_t in disk/disk_internal.h, which carries the layout verified
 * against the machine code (0x48 bytes at DISK_$DATA + N*0x48 + 0x7c).
 */

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

/* Layout recovered from the disassembly -- see the field comments above. */
#if defined(ARCH_M68K)
_Static_assert(__builtin_offsetof(disk_device_entry_t, jump_table) == 0x00, "disk_device_entry_t.jump_table");
_Static_assert(__builtin_offsetof(disk_device_entry_t, device_type) == 0x04, "disk_device_entry_t.device_type");
_Static_assert(__builtin_offsetof(disk_device_entry_t, controller) == 0x06, "disk_device_entry_t.controller");
_Static_assert(__builtin_offsetof(disk_device_entry_t, unit_count) == 0x08, "disk_device_entry_t.unit_count");
_Static_assert(__builtin_offsetof(disk_device_entry_t, flags) == 0x0A, "disk_device_entry_t.flags");
/* Stride 0x0C, 32 entries: DISK_$REGISTER walks the table with
 * `lea (0xc,A0),A0` / `moveq #0x1f,D1` / `dbf` (00e3d9e8..00e3da0e). */
_Static_assert(sizeof(disk_device_entry_t) == 0x0C, "disk_device_entry_t size");
#endif

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
  /* +0x04: shutdown(controller, unit); called by DISK_$SHUTDOWN (0xe3dc36) */
  void (*shutdown)(uint16_t controller, uint16_t unit);
  void *dinit;      /* +0x08: Device init function */
  void *_reserved3; /* +0x0c */
  void *do_io;      /* +0x10: I/O function */
} disk_jump_table_t;

/* Layout recovered from the disassembly -- see the field comments above. */
#if defined(ARCH_M68K)
_Static_assert(__builtin_offsetof(disk_jump_table_t, _reserved1) == 0x00, "disk_jump_table_t._reserved1");
_Static_assert(__builtin_offsetof(disk_jump_table_t, dinit) == 0x08, "disk_jump_table_t.dinit");
_Static_assert(__builtin_offsetof(disk_jump_table_t, _reserved3) == 0x0C, "disk_jump_table_t._reserved3");
_Static_assert(__builtin_offsetof(disk_jump_table_t, do_io) == 0x10, "disk_jump_table_t.do_io");
#endif

/*
 * disk_$per_proc_t - the per-process I/O slot inside DISK_$DATA
 *
 * DISK_$DATA + 0x378 starts an array of 0x1C-byte slots indexed by
 * PROC1_$CURRENT.  DISK_$WRITE_MULTI (0x00E3CCEE) and DISK_$READ_MULTI
 * (0x00E3CFCC) form the address the same way, with the index scaled to
 * 0x1C by "id*32 - id*4" and used as a sign-extended word:
 *
 *   00e3ce2a  move.w (0x00e20608).l,D0w     ; PROC1_$CURRENT
 *   00e3ce30  lsl.w #0x2,D0w / move.w D0w,D4w / neg.w D0w
 *   00e3ce36  lsl.w #0x3,D4w / add.w D4w,D0w ; D0 = id * 0x1C
 *   00e3ce3a  lea (0x0,A5,D0w*0x1),A3
 *   00e3ce3e  move.l (0x378,A3),D5           ; io_ec.value
 *   00e3ce46  move.l (0x384,A3),D4           ; err_ec.value
 *   00e3cd3a  st (0x390,A2)                  ; io_pending = true
 *   00e3ced2  tst.b (0x390,A0) / bmi         ; still pending -> return
 *
 * so the three field offsets 0x378 / 0x384 / 0x390 are 0x00 / 0x0C / 0x18
 * within the slot.  disk/disk_internal.h names the same two eventcounts as
 * DMOD_PER_PROC_IO_EC and DMOD_PER_PROC_ERR_EC.
 *
 * The array runs from DISK_$DATA + 0x378 up to the disk_$error_info_t record
 * at DISK_$DATA + 0xA94, which is 0x1C * 0x41 bytes, so there are 65 slots -
 * index 0 plus the 64 process ids PROC1_$CURRENT can take.
 *
 * The four drivers that retire a request clear the pending byte for the
 * process the REQUEST names (its byte at +0x1E), with the identical five
 * instructions and A1 = 0x00E7A560, i.e. slot base + 0x18 - 4 + 0x1C*id:
 * WIN_$FORMAT_TRACK 0x00E19764, WIN_$DO_IO 0x00E1994A and FLP_FORMAT_TRACK
 * 0x00E3DDB0 / 0x00E3DFBC.  The index is the same 0-based process id, NOT a
 * volume number.
 */
#define DISK_PER_PROC_BASE_OFFSET   0x378
#define DISK_PER_PROC_ENTRIES       0x41    /* ids 0..64 */

typedef struct disk_$per_proc_t {
    ec_$eventcount_t    io_ec;      /* 0x00 (A5+0x378): I/O completion */
    ec_$eventcount_t    err_ec;     /* 0x0C (A5+0x384): error notification */
    boolean             io_pending; /* 0x18 (A5+0x390): set by DISK before it
                                     *       issues the requests, cleared by
                                     *       the driver as each is retired */
    uint8_t             _pad19[3];  /* 0x19: to the 0x1C stride */
} disk_$per_proc_t;

/*
 * ec_$eventcount_t holds two native pointers, so the slot is 0x1C bytes only
 * on the 32-bit target.
 */
#if defined(ARCH_M68K)
_Static_assert(__builtin_offsetof(disk_$per_proc_t, io_ec) == 0x00, "disk_$per_proc_t.io_ec");
_Static_assert(__builtin_offsetof(disk_$per_proc_t, err_ec) == 0x0C, "disk_$per_proc_t.err_ec");
_Static_assert(__builtin_offsetof(disk_$per_proc_t, io_pending) == 0x18, "disk_$per_proc_t.io_pending");
_Static_assert(sizeof(disk_$per_proc_t) == 0x1C, "disk_$per_proc_t must be 28 bytes");
#endif

/*
 * 0x00E7A544 == DISK_$DATA (0x00E7A1CC) + 0x378.  On the target the array is
 * at that fixed address; a host build gets a real object so a test can supply
 * it (the same shape the rest of the tree uses for A5-relative tables).
 */
#define DISK_PER_PROC_VA 0x00E7A544
#if defined(ARCH_M68K)
#define DISK_$PER_PROC ((disk_$per_proc_t *)DISK_PER_PROC_VA)
#else
extern disk_$per_proc_t DISK_$PER_PROC[DISK_PER_PROC_ENTRIES];
#endif

/*
 * Global data areas
 */

/* Disk subsystem base at 0xe7a1cc */
extern uint8_t DISK_$DATA[];

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
void DISK_$SORT(void *dev_entry, void **queue_ptr);

/*
 * Internal queue block operations (used by AST subsystem)
 * These have different signatures from the public wrappers
 */
/*
 * DISK_$GET_QBLKS (0x00E3BFF4) - allocate a chain of disk queue blocks.
 *
 * Both out-parameters are 32-bit target VA cells, not host pointers: the
 * callee stores them with a single `move.l` each (0x00E3BF7E, 0x00E3BFB8 in
 * disk_$get_qblks_internal) and reloads last_out at that width (0x00E3BFD8).
 * Convert to a host pointer with ARCH_VA_TO_PTR.  The head cell is signed and
 * the tail unsigned only because that is how the callers declare them; the
 * two are the same four-byte cell.
 */
void DISK_$GET_QBLKS(int16_t count, int32_t *qblk_head, uint32_t *qblk_tail);
void DISK_$RTN_QBLKS(int16_t count, int32_t qblk_head, uint32_t qblk_tail);

/*
 * DISK_INTERRUPT - disk interrupt dispatcher (m68k vector IO_VECTOR_DISK)
 *
 * Original address: 0x00e0aabc
 */
void DISK_INTERRUPT(void);

/*
 * I/O operations
 *
 * Argument order verified against DISK_$READ (0xe3cf64) and DISK_$WRITE
 * (0xe3cc78): the disk address is argument 2 ((0xa,A6)) and the physical
 * page number argument 3 ((0xe,A6)); DISK_IO receives them the other way
 * round (0xe3cfac / 0xe3ccac).
 */
void DISK_$READ(int16_t vol_idx, uint32_t daddr, uint32_t ppn, uint32_t *info,
                status_$t *status);
void DISK_$WRITE(int16_t vol_idx, uint32_t daddr, uint32_t ppn, uint32_t *info,
                 status_$t *status);

/*
 * Internal multi-block I/O (used by AST subsystem)
 * Takes queue block head/tail from DISK_$GET_QBLKS
 */
void DISK_$READ_MULTI(uint16_t vol_idx, int16_t flags1, int16_t flags2,
                      int32_t qblk_head, uint32_t qblk_tail,
                      int16_t *pages_read, status_$t *status);
void DISK_$WRITE_MULTI(int8_t flags, void *req_list, status_$t *status);
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
/*
 * DISK_$SHUTDOWN - Shut a disk device down through its driver
 *
 * @param dev_info Device registration entry (jump table at +0x00,
 *                 controller number at +0x06)
 * @param unit     Device unit number
 *
 * Original address: 0x00e3dc28
 */
void DISK_$SHUTDOWN(disk_device_entry_t *dev_info, uint16_t unit);
/* 0x00E3DB04 reads no parameters and returns nothing. */
void DISK_$SPIN_DOWN(void);
/* DISK_$REVALID takes a disk_$volume_t *, so it is declared in
 * disk/disk_internal.h; nothing outside disk/ calls it. */
void DISK_$WRITE_PROTECT(int16_t mode, int16_t vol_idx, status_$t *status);
void DISK_$GET_STATS(int16_t dev_type, int16_t controller, uint8_t *has_stats, void *stats);
void DISK_$UNASSIGN(uint16_t *vol_idx_ptr, status_$t *status);
void DISK_$UNASSIGN_ALL(void);
void DISK_$REVALIDATE(int16_t vol_idx);
void DISK_$DISMOUNT(uint16_t vol_idx);
void DISK_$GET_ERROR_INFO(void *buffer);
void DISK_$LVUID_TO_VOLX(void *uid_ptr, int16_t *vol_idx, status_$t *status);

/* Volume assignment operations */
int16_t DISK_$PV_MOUNT(int16_t dev, int16_t bus, int16_t ctlr,
                       status_$t *status);
int16_t DISK_$LV_MOUNT(uid_t *lv_uid, status_$t *status_ret);
void DISK_$LV_UID(int16_t vol_idx, int16_t lv_num, uid_t *uid_ret,
                  status_$t *status);
void DISK_$PV_ASSIGN_N(int16_t *unit_type_ptr, int16_t *device_ptr,
                       int16_t *unit_ptr, uint16_t *flags_ptr,
                       uint16_t *vol_idx_ptr, uint32_t *num_blocks_ptr,
                       uint16_t *sec_per_track_ptr, uint16_t *num_heads_ptr,
                       uint32_t *pvlabel_info, status_$t *status);
void DISK_$PV_ASSIGN(int16_t *unit_type_ptr, int16_t *device_ptr,
                     int16_t *unit_ptr, uint16_t *vol_idx_ptr,
                     int32_t *info_ptr, uint32_t *num_blocks_ptr,
                     uint16_t *sec_per_track_ptr, status_$t *status);
uint16_t DISK_$LV_ASSIGN(uint16_t *vol_idx_ptr, uint16_t *lv_idx_ptr,
                         int32_t *blocks_avail_ptr, status_$t *status);

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
 * DISK_$DO_CHKSUM - Disk checksum enable flag (negative = checksums on)
 *
 * Read/temporarily cleared by PMAP_$PURIFIER_L and pmap_$fill_write_qblks.
 * Original address: 0xE7ACCC (1 byte)
 */
extern int8_t DISK_$DO_CHKSUM;

/*
 * DISK_$DIAG - Diagnostic mode flag
 *
 * Enables the privileged diagnostic paths: DISK_$DIAG_IO tests it as a
 * Domain boolean (`< 0`), while STOP_$WATCH's peek/poke branch table gates
 * the three poke operations on it being merely non-zero
 * (0x00E8186A: `tst.b (0x00e7acca).l / bne`).
 *
 * Original address: 0xE7ACCA (1 byte)
 */
extern int8_t DISK_$DIAG;

#endif /* DISK_H */
