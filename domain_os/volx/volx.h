/*
 * VOLX - Volume Index Management
 *
 * This module manages the volume index table, which tracks mounted logical
 * volumes and their associated UIDs and device information. It coordinates
 * mounting/dismounting operations between the DISK, VTOC, DIR, and AST
 * subsystems.
 *
 * The VOLX table contains up to 6 mounted volumes (indices 1-6).
 * Each entry stores:
 *   - Root directory UID of the volume
 *   - Logical volume UID
 *   - Parent directory UID (mount point)
 *   - Physical device location (dev, bus, controller, lv_num)
 *
 * Memory layout (m68k):
 *   - VOLX table base: 0xE82604
 *   - Entry size: 32 bytes (0x20)
 *   - Max entries: 6 (indices 1-6, index 0 unused)
 */

#ifndef VOLX_H
#define VOLX_H

#include "base/base.h"
#include "uid/uid.h"
#include "cal/cal.h"

/*
 * VOLX table constants
 */
#define VOLX_MAX_VOLUMES 6   /* Maximum mounted volumes (1-6) */
#define VOLX_ENTRY_SIZE 0x20 /* 32 bytes per entry */

/*
 * Status codes (module 0x14 = volume subsystem)
 */
#define status_$volume_logical_vol_not_mounted 0x00140003
#define status_$volume_unable_to_dismount_boot_volume 0x00140002
#define status_$volume_entry_directory_not_on_logical_volume 0x00140004
#define status_$volume_physical_vol_replaced_since_mount 0x00140005
#define status_$volume_cant_stream_this_object_type 0x00140006
#define status_$volume_no_more_streams 0x00140007

/*
 * Additional status codes used by VOLX
 */
#define status_$disk_already_mounted 0x0008001e
#define status_$storage_module_stopped 0x0008001b
#define status_$directory_is_full 0x000e0002
#define status_$name_already_exists 0x000e0003
#define status_$stream_no_more_streams 0x00140007
#define status_$stream_cant_stream_this_object_type 0x00140006

/*
 * VOLX table entry structure
 *
 * Represents a mounted volume with its UIDs and device location.
 * Size: 32 bytes (0x20)
 */
typedef struct volx_entry_t {
  uid_t dir_uid;    /* 0x00: Root directory UID */
  uid_t lv_uid;     /* 0x08: Logical volume UID */
  uid_t parent_uid; /* 0x10: Parent directory UID (mount point) */
  int16_t dev;      /* 0x18: Device unit number */
  int16_t bus;      /* 0x1A: Bus/controller number */
  int16_t ctlr;     /* 0x1C: Controller type */
  int16_t lv_num;   /* 0x1E: Logical volume number */
} volx_entry_t;

/*
 * Global data
 */

/* VOLX table base address (0xE82604 on m68k) */
#if defined(M68K)
#define VOLX_$TABLE_BASE ((volx_entry_t *)0xE82604)
#else
extern volx_entry_t *volx_table_base;
#define VOLX_$TABLE_BASE volx_table_base
#endif

/* Boot volume index (the volume containing the OS)
 * Declared in cal/cal.h, included above */

/*
 * ============================================================================
 * Function Prototypes - Public API
 * ============================================================================
 */

/*
 * VOLX_$MOUNT - Mount a logical volume
 *
 * Coordinates mounting a volume by calling DISK_$PV_MOUNT, DISK_$LV_MOUNT,
 * VTOC_$MOUNT, and optionally DIR_$ADD_MOUNT. On success, populates the
 * VOLX table entry.
 *
 * Parameters:
 *   dev         - Pointer to device unit number
 *   bus         - Pointer to bus/controller number
 *   ctlr        - Pointer to controller type
 *   lv_num      - Pointer to logical volume number
 *   salvage_ok  - Pointer to salvage flag (passed to VTOC_$MOUNT)
 *   write_prot  - Pointer to write protect flag (negative = write protected)
 *   parent_uid  - Pointer to parent directory UID (or UID_$NIL if no mount
 * point) dir_uid_ret - Output: receives root directory UID status      -
 * Output: status code
 *
 * Original address: 0x00E6B118
 */
void VOLX_$MOUNT(int16_t *dev, int16_t *bus, int16_t *ctlr, int16_t *lv_num,
                 int8_t *salvage_ok, int8_t *write_prot, uid_t *parent_uid,
                 uid_t *dir_uid_ret, status_$t *status);

/*
 * VOLX_$DISMOUNT - Dismount a logical volume
 *
 * Dismounts a volume by its physical location. Validates that the volume
 * is still at the expected location, removes the mount point link, and
 * calls AST_$DISMOUNT to flush cached data.
 *
 * Parameters:
 *   dev         - Pointer to device unit number
 *   bus         - Pointer to bus/controller number
 *   ctlr        - Pointer to controller type
 *   lv_num      - Pointer to logical volume number
 *   entry_uid   - Pointer to expected entry UID (or UID_$NIL to skip check)
 *   force       - Pointer to force flag (negative = skip disk change check)
 *   status      - Output: status code
 *
 * Original address: 0x00E6B346
 */
void VOLX_$DISMOUNT(int16_t *dev, int16_t *bus, int16_t *ctlr, int16_t *lv_num,
                    uid_t *entry_uid, int8_t *force, status_$t *status);

/*
 * VOLX_$SHUTDOWN - Dismount all volumes
 *
 * Iterates through all mounted volumes and dismounts them.
 * Returns the first error encountered (but continues with remaining volumes).
 *
 * Returns:
 *   First error status encountered, or status_$ok if all succeeded
 *
 * Original address: 0x00E6B508
 */
status_$t VOLX_$SHUTDOWN(void);

/*
 * VOLX_$GET_INFO - Get volume information
 *
 * Returns the root directory UID and free/total block counts for a volume.
 *
 * Parameters:
 *   vol_idx     - Pointer to volume index (1-6)
 *   dir_uid_ret - Output: receives root directory UID
 *   free_blocks - Output: receives free block count
 *   total_blocks - Output: receives total block count
 *   status      - Output: status code
 *
 * Original address: 0x00E6B5C6
 */
void VOLX_$GET_INFO(int16_t *vol_idx, uid_t *dir_uid_ret, uint32_t *free_blocks,
                    uint32_t *total_blocks, status_$t *status);

/*
 * VOLX_$GET_UIDS - Get volume UIDs by physical location
 *
 * Looks up a volume by its physical device location and returns
 * both the logical volume UID and root directory UID.
 *
 * Parameters:
 *   dev         - Pointer to device unit number
 *   bus         - Pointer to bus/controller number
 *   ctlr        - Pointer to controller type
 *   lv_num      - Pointer to logical volume number
 *   lv_uid_ret  - Output: receives logical volume UID
 *   dir_uid_ret - Output: receives root directory UID
 *   status      - Output: status code
 *
 * Original address: 0x00E6B62C
 */
void VOLX_$GET_UIDS(int16_t *dev, int16_t *bus, int16_t *ctlr, int16_t *lv_num,
                    uid_t *lv_uid_ret, uid_t *dir_uid_ret, status_$t *status);

/*
 * VOLX_$REC_ENTRY - Record entry directory UID
 *
 * Updates the root directory UID for a mounted volume.
 * Used to update the entry after initial mount.
 *
 * Parameters:
 *   vol_idx     - Pointer to volume index (1-6)
 *   dir_uid     - Pointer to new root directory UID
 *
 * Original address: 0x00E6B6B2
 */
void VOLX_$REC_ENTRY(int16_t *vol_idx, uid_t *dir_uid);

/*
 * ============================================================================
 * Internal Functions
 * ============================================================================
 */

/*
 * FIND_VOLX - Find volume index by physical location
 *
 * Searches the VOLX table for a volume matching the given physical
 * device location.
 *
 * Parameters:
 *   dev         - Device unit number
 *   bus         - Bus/controller number
 *   ctlr        - Controller type
 *   lv_num      - Logical volume number
 *
 * Returns:
 *   Volume index (1-6) if found, 0 if not found
 *
 * Original address: 0x00E6B0BC
 */
int16_t FIND_VOLX(int16_t dev, int16_t bus, int16_t ctlr, int16_t lv_num);

#endif /* VOLX_H */
