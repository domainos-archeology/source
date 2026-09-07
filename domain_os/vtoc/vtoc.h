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
 *   - Old format: 0xCC (204) bytes per on-disk entry
 *   - New format: 0x150 (336) bytes per on-disk entry with extended ACL
 *     support, of which only the first 0x90 bytes are the VTOCE the API
 *     passes around (see vtoce_$result_t)
 */

#ifndef VTOC_H
#define VTOC_H

#include "base/base.h"
#include "uid/uid.h"

/*
 * VTOCE read result structure -- 0x90 (144) bytes.
 *
 * Contains the VTOCE data in new format, regardless of on-disk format.
 * Old format VTOCEs are converted to new format on read.
 *
 * The record the VTOCE API hands across the caller boundary is NOT the whole
 * 0x150-byte on-disk entry: that is only the entry *stride* inside a VTOCE
 * block.  Every routine that moves an entry to or from a caller's buffer
 * moves exactly 36 longwords:
 *
 *   VTOCE_$READ      0x00E395B0  moveq #0x23,D4 / move.l (A1)+,(A0)+ / dbf
 *                                (from block + 8 + idx*0x150 into *result)
 *   VTOCE_$WRITE     0x00E3977A  moveq #0x23,D1 / move.l (A0)+,(A1)+ / dbf
 *   VTOC_$ALLOCATE   0x00E38C26  moveq #0x23,D6 / move.l (A0)+,(A4)+ / dbf
 *
 * and the two format converters agree: VTOCE_$OLD_TO_NEW's highest write to
 * its destination is the longword at +0x8C (0x00E19F32) after clearing 31
 * longwords from +0x14 to +0x90 (0x00E19E72), and VTOCE_$NEW_TO_OLD's highest
 * read from its source is +0x88 (0x00E38556).
 *
 * This is also exactly what OS_$INIT provides: it clears 36 longwords at
 * A6-0x128 (0x00E34018) and the next frame slot, the lookup request, starts
 * at A6-0x98 = A6-0x128 + 0x90.
 *
 * Known fields (offsets recovered from VTOCE_$READ, VTOCE_$OLD_TO_NEW and
 * OS_$INIT; the record is left opaque because callers index it by offset):
 *   +0x00 byte  object type
 *   +0x01 byte  flags (low nibble := 1 by VTOCE_$READ's caller)
 *   +0x02 word  status; bit 15 = entry in use, +0x03 bit 1 = volume read-only
 *               (VTOCE_$READ 0x00E395D8)
 *   +0x04 uid   object UID
 *   +0x14 long  length / current size (OS_$INIT 0x00E34048)
 *   +0x65 byte  bit 7 = the old-format "directory" bit (0x00E19F46)
 *   +0x74 word  block count (OS_$INIT 0x00E3404C, OLD_TO_NEW 0x00E19ECC)
 *   +0x88 uid   ACL UID (OS_$INIT 0x00E3403A)
 */
typedef struct vtoce_$result_t {
    uint8_t     data[0x90];         /* VTOCE data in new format (144 bytes) */
} vtoce_$result_t;

#if defined(ARCH_M68K)
_Static_assert(sizeof(vtoce_$result_t) == 0x90,
               "vtoce_$result_t must be 0x90 bytes (36 longwords)");
#endif

/*
 * VTOC lookup request structure (0x20 bytes)
 *
 * This is the object location descriptor also embedded in AST entries
 * (aote + 0x9C).  Layout verified against VTOC_$LOOKUP / VTOC_$SET_NAME_DIRS
 * / VTOC_$SEARCH_VOLUMES: the UID is at +0x08, the block hint at +0x04 and
 * the volume index byte at +0x1C.
 */
typedef struct vtoc_$lookup_req_t {
    uint32_t    flags;              /* 0x00: type/flags word; cleared on success, then
                                             byte 1 low nibble := 1 and bytes 2-3 := the
                                             per-volume word at OS_DISK_DATA[vol_idx*2-2] */
    uint32_t    block_hint;         /* 0x04: VTOC block << 4 | entry index (0 for hash lookup) */
    uid_t       uid;                /* 0x08: UID to look up */
    uint32_t    port;               /* 0x10: ROUTE_$PORT (filled on success) */
    uint32_t    node;               /* 0x14: NODE_$ME (filled on success) */
    uint32_t    reserved_18;        /* 0x18: cleared on success */
    uint8_t     vol_idx;            /* 0x1C: Volume index (1-based); on success rewritten
                                             with the cache entry index */
    uint8_t     flags_1d;           /* 0x1D: bit 6 set on success, low nibble := 1 */
    uint16_t    reserved_1e;        /* 0x1E */
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
 * Allocates a new VTOC entry for the object described by new_vtoce and
 * writes it into the volume named by loc->vol_idx.  Finds free space in the
 * existing VTOC/bucket chain or allocates new blocks.
 *
 * @param loc       In: loc->vol_idx selects the volume and loc->block_hint
 *                  supplies the VTOCE-location hint.  Out: the whole 0x20-byte
 *                  object-location descriptor is rewritten on EVERY exit path
 *                  (0xE38F24-0xE38F72), whether or not the allocation
 *                  succeeded; loc->uid is left untouched.
 * @param new_vtoce In: the new-format VTOCE image to install (0x90 bytes, i.e.
 *                  a vtoce_$result_t: 0x00E38C26 copies exactly 36 longwords
 *                  to the block entry).  The object UID is at +4.
 *                  VTOC_$ALLOCATE sets its in-use bit and type byte.
 * @param status    Output status code
 *
 * Original address: 0x00e388ac
 */
void VTOC_$ALLOCATE(vtoc_$lookup_req_t *loc, void *new_vtoce, status_$t *status);

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
 * Converts a 0xCC byte old format VTOCE to the 0x90 byte in-core new format.
 * Sets default values for fields not present in old format.
 *
 * @param old_vtoce Pointer to old format VTOCE (0xCC bytes)
 * @param new_vtoce Pointer to receive the new format VTOCE (0x90 bytes; the
 *                  highest store is the longword at +0x8C, 0x00E19F32)
 *
 * Original address: 0x00e19db8
 */
void VTOCE_$OLD_TO_NEW(void *old_vtoce, void *new_vtoce);

/*
 * VTOCE_$NEW_TO_OLD - Convert new format VTOCE to old format
 *
 * Converts a 0x90 byte new format VTOCE to 0xCC byte old format.
 * Some fields are lost in the conversion.
 *
 * @param new_vtoce Pointer to new format VTOCE (0x90 bytes; the highest load
 *                  is the longword at +0x88, 0x00E38556)
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

/* Nil owner/org UIDs (defined in vtoc/vtoc_data.c; also used by file/) */
extern uid_t PPO_$NIL_USER_UID;     /* 0xE174EC: Nil user UID */
extern uid_t PPO_$NIL_ORG_UID;      /* 0xE17574: Nil org UID */

/*
 * UID constants for VTOC block types (moved here from vtoc_internal.h:
 * bat/ and other subsystems reference them, so they belong in the public
 * header -- bead source-3uo).
 */
extern uid_t VTOC_$UID;             /* 0xE1739C: VTOC block UID */
extern uid_t VTOC_BKT_$UID;         /* 0xE173AC: VTOC bucket UID */

#endif /* VTOC_H */
