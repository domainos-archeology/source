/*
 * VTOC - Volume Table of Contents
 *
 * This module manages volume mounting/dismounting and VTOCE (Volume Table
 * of Contents Entry) operations for file metadata.
 *
 * The VTOC maintains:
 *   - File metadata (name, size, timestamps, permissions)
 *   - File block mappings (direct and indirect blocks)
 *   - Directory structure information
 *
 * Two VTOCE formats are supported:
 *   - Old format: 0xCC (204) bytes per entry
 *   - New format: 0x150 (336) bytes per entry with extended ACL support
 */

#ifndef VTOC_H
#define VTOC_H

#include "base/base.h"
#include "uid/uid.h"

/*
 * VTOCE read result structure
 *
 * Contains the VTOCE data in new format, regardless of on-disk format.
 * Old format VTOCEs are converted to new format on read.
 */
typedef struct vtoce_$result_t {
    uint8_t     data[0x150];        /* VTOCE data in new format (336 bytes) */
} vtoce_$result_t;

/*
 * VTOC lookup request structure
 */
typedef struct vtoc_$lookup_req_t {
    uid_t       uid;                /* 0x00: UID to look up */
    uint32_t    block_hint;         /* 0x08: Block hint (0 for hash lookup) */
    uint8_t     vol_idx;            /* 0x0C: Volume index */
} vtoc_$lookup_req_t;

/*
 * ============================================================================
 * Volume Management Functions
 * ============================================================================
 */

/*
 * VTOC_$MOUNT - Mount a volume's VTOC
 *
 * Initializes the VTOC subsystem for a volume. Must be called after
 * BAT_$MOUNT and before any file operations.
 *
 * @param vol_idx   Volume index (0-7)
 * @param param_2   Mount parameter 2
 * @param param_3   Mount flags
 * @param param_4   Write protection flag (negative = set write protect)
 * @param status    Output status code
 *
 * Original address: 0x00e38584
 */
void VTOC_$MOUNT(int16_t vol_idx, uint16_t param_2, uint8_t param_3, char param_4,
                 status_$t *status);

/*
 * VTOC_$DISMOUNT - Dismount a volume's VTOC
 *
 * Flushes cached VTOC data and marks the volume as dismounted.
 *
 * @param vol_idx   Volume index (0-7)
 * @param flags     Dismount flags (bit 7 = force)
 * @param status    Output status code
 *
 * Original address: 0x00e38764
 */
void VTOC_$DISMOUNT(uint16_t vol_idx, uint8_t flags, status_$t *status);

/*
 * ============================================================================
 * VTOC Allocation and Lookup Functions
 * ============================================================================
 */

/*
 * VTOC_$ALLOCATE - Allocate a new VTOCE
 *
 * Allocates a new VTOC entry for a file or directory.
 * Finds free space in existing VTOC blocks or allocates new blocks.
 *
 * @param req       Allocation request (includes UID, parent UID, vol_idx)
 * @param result    Receives the new VTOCE data
 * @param status    Output status code
 *
 * Original address: 0x00e388ac
 */
void VTOC_$ALLOCATE(void *req, vtoce_$result_t *result, status_$t *status);

/*
 * VTOC_$LOOKUP - Look up a VTOCE by UID
 *
 * Searches for a VTOCE with the given UID. Uses hash-based lookup
 * on new format volumes or linear search on old format volumes.
 *
 * @param req       Lookup request (uid, block_hint, vol_idx)
 * @param status    Output status code
 *
 * Original address: 0x00e38f80
 */
void VTOC_$LOOKUP(vtoc_$lookup_req_t *req, status_$t *status);

/*
 * VTOC_$GET_UID - Get UID from VTOCE location
 *
 * Retrieves the UID of a VTOCE given its block and entry index.
 *
 * @param vol_idx   Volume index
 * @param vtoc_idx  VTOC index (bucket for new format)
 * @param entry_idx Entry index within block
 * @param uid_ret   Receives the UID
 * @param status    Output status code
 *
 * Original address: 0x00e391f2
 */
void VTOC_$GET_UID(int16_t *vol_idx, uint16_t *vtoc_idx, uint32_t *entry_idx,
                   uid_t *uid_ret, status_$t *status);

/*
 * ============================================================================
 * Name Directory Functions
 * ============================================================================
 */

/*
 * VTOC_$GET_NAME_DIRS - Get name directory UIDs
 *
 * Retrieves the UIDs of the two name directory objects for a volume.
 * These are used for pathname resolution.
 *
 * @param vol_idx   Volume index
 * @param dir1_uid  Receives first directory UID
 * @param dir2_uid  Receives second directory UID
 * @param status    Output status code
 *
 * Original address: 0x00e393ee
 */
void VTOC_$GET_NAME_DIRS(int16_t vol_idx, uid_t *dir1_uid, uid_t *dir2_uid,
                         status_$t *status);

/*
 * VTOC_$SET_NAME_DIRS - Set name directory UIDs
 *
 * Updates the name directory UIDs for a volume.
 *
 * @param vol_idx   Volume index
 * @param dir1_uid  First directory UID
 * @param dir2_uid  Second directory UID
 * @param status    Output status code
 *
 * Original address: 0x00e39486
 */
void VTOC_$SET_NAME_DIRS(int16_t vol_idx, uid_t *dir1_uid, uid_t *dir2_uid,
                         status_$t *status);

/*
 * ============================================================================
 * VTOCE Read/Write Functions
 * ============================================================================
 */

/*
 * VTOCE_$READ - Read a VTOCE
 *
 * Reads a VTOCE given lookup request. Converts old format to new format
 * if necessary.
 *
 * @param req       Lookup request with block location
 * @param result    Receives the VTOCE data (always new format)
 * @param status    Output status code
 *
 * Original address: 0x00e394ec
 */
void VTOCE_$READ(vtoc_$lookup_req_t *req, vtoce_$result_t *result,
                 status_$t *status);

/*
 * VTOCE_$WRITE - Write a VTOCE
 *
 * Writes VTOCE data back to disk. Converts from new format to old format
 * if the volume uses old format.
 *
 * @param req       Request with block location
 * @param data      VTOCE data to write (new format)
 * @param flags     Write flags (bit 7 = immediate writeback)
 * @param status    Output status code
 *
 * Original address: 0x00e396d6
 */
void VTOCE_$WRITE(vtoc_$lookup_req_t *req, vtoce_$result_t *data, char flags,
                  status_$t *status);

/*
 * ============================================================================
 * VTOCE Format Conversion Functions
 * ============================================================================
 */

/*
 * VTOCE_$OLD_TO_NEW - Convert old format VTOCE to new format
 *
 * Converts a 0xCC byte old format VTOCE to 0x150 byte new format.
 * Sets default values for fields not present in old format.
 *
 * @param old_vtoce Pointer to old format VTOCE (0xCC bytes)
 * @param new_vtoce Pointer to receive new format VTOCE (0x150 bytes)
 *
 * Original address: 0x00e19db8
 */
void VTOCE_$OLD_TO_NEW(void *old_vtoce, void *new_vtoce);

/*
 * VTOCE_$NEW_TO_OLD - Convert new format VTOCE to old format
 *
 * Converts a 0x150 byte new format VTOCE to 0xCC byte old format.
 * Some fields are lost in the conversion.
 *
 * @param new_vtoce Pointer to new format VTOCE (0x150 bytes)
 * @param flags     Conversion flags (bit 7 = use alternate parent)
 * @param old_vtoce Pointer to receive old format VTOCE (0xCC bytes)
 *
 * Original address: 0x00e384c4
 */
void VTOCE_$NEW_TO_OLD(void *new_vtoce, char *flags, void *old_vtoce);

/*
 * ============================================================================
 * Volume Search Functions
 * ============================================================================
 */

/*
 * VTOC_$SEARCH_VOLUMES - Search volumes for an object
 *
 * Searches volumes 1-5 for an object via VTOC_$LOOKUP.
 * Used during force-activation path for root objects.
 *
 * @param uid_info  Pointer to UID info structure
 * @param status    Output status code (file_$object_not_found if not found)
 *
 * Original address: 0x00E01BEE
 */
void VTOC_$SEARCH_VOLUMES(void *uid_info, status_$t *status);

/*
 * ============================================================================
 * File Map Functions
 * ============================================================================
 */

/*
 * VTOCE_$LOOKUP_FM - Look up block in file map
 *
 * Given a VTOCE location and logical block number, returns the physical
 * disk block. Handles direct, indirect, and double indirect blocks.
 *
 * @param vtoce_loc VTOCE location (block << 4 | entry)
 * @param block_num Logical block number within file
 * @param flags     Lookup flags
 * @param phys_block Receives physical block number
 * @param alloc_count Receives allocation count (if allocating)
 * @param status    Output status code
 *
 * Original address: 0x00e39a04
 */
void VTOCE_$LOOKUP_FM(void *vtoce_loc, uint16_t block_num, uint16_t flags,
                      uint32_t *phys_block, uint32_t *alloc_count,
                      status_$t *status);

/*
 * VTOCE_$TRUNCATE - Truncate a file
 *
 * Frees file blocks beyond the specified length and updates the VTOCE.
 * If new_length is negative, deletes the VTOCE entirely.
 *
 * @param vtoce_loc VTOCE location (block << 4 | entry)
 * @param flags     Truncate flags
 * @param new_length New file length in bytes (-1 = delete)
 * @param param_4   Additional parameter
 * @param blocks_freed Receives count of blocks freed
 * @param status    Output status code
 *
 * Original address: 0x00e39e42
 */
void VTOCE_$TRUNCATE(void *vtoce_loc, uint32_t flags, int32_t new_length,
                     int32_t param_4, uint32_t *blocks_freed,
                     status_$t *status);

#endif /* VTOC_H */
