/*
 * VTOC Internal Header
 *
 * Internal data structures and declarations for the Volume Table of Contents.
 * This header should only be included by VTOC implementation files.
 *
 * The VTOC manages file metadata (VTOCE - VTOC Entries) on disk volumes.
 * Two formats exist:
 *   - Old format: 0xCC (204) bytes per VTOCE, 5 entries per block
 *   - New format: 0x150 (336) bytes per VTOCE, variable entries per bucket
 */

#ifndef VTOC_INTERNAL_H
#define VTOC_INTERNAL_H

#include "vtoc/vtoc.h"
#include "audit/audit.h"
#include "ml/ml.h"
#include "dbuf/dbuf.h"
#include "disk/disk.h"
#include "bat/bat.h"
#include "uid/uid.h"
#include "time/time.h"
#include "os/os.h"

/*
 * Lock ID for disk operations
 */
#define VTOC_LOCK_ID    0x10    /* ML_$LOCK ID for DISK operations */

/*
 * Buffer dirty flag values (from bat_internal.h)
 */
#define BAT_BUF_CLEAN       8       /* Buffer is clean, release without write */
#define BAT_BUF_DIRTY       9       /* Buffer is dirty, write on release */
#define BAT_BUF_WRITEBACK   0xB     /* Write back immediately */

/*
 * VTOCE entry sizes
 */
#define VTOCE_OLD_SIZE      0xCC    /* 204 bytes - old format entry size */
#define VTOCE_NEW_SIZE      0x150   /* 336 bytes - new format entry size */

/*
 * Entries per block
 */
#define VTOCE_OLD_ENTRIES_PER_BLOCK     5   /* Old format: 5 entries per 1024-byte block */
#define VTOCE_NEW_ENTRIES_PER_BUCKET    4   /* New format: 4 entries per bucket slot */
#define VTOCE_BUCKET_SLOTS              20  /* 20 UID slots per bucket entry */

/*
 * Bucket entry size (new format)
 * Each bucket has: next pointer, slot count, then 20 UID entries (12 bytes each)
 */
#define VTOC_BUCKET_ENTRY_SIZE  0xF8    /* 248 bytes per bucket entry */

/*
 * File map levels for indirect block addressing
 */
#define FM_DIRECT_BLOCKS        8       /* Direct block pointers (level 1) */
#define FM_INDIRECT_BLOCKS      0x800   /* Single indirect (level 2) */
#define FM_DOUBLE_INDIRECT      0x10000 /* Double indirect (level 3) */

/*
 * Block count thresholds for file map levels
 */
#define FM_LEVEL1_MAX           0x20    /* 32 direct blocks */
#define FM_LEVEL2_MAX           0x120   /* 288 blocks (32 + 256 indirect) */
#define FM_LEVEL3_MAX           0x10120 /* 65824 blocks (32 + 256 + 65536 double indirect) */

/*
 * Status codes
 */
#define status_$VTOC_not_mounted    0x20001     /* VTOC not mounted */
#define status_$VTOC_not_found      0x20005     /* VTOCE not found in chain */
#define status_$VTOC_invalid_vtoce  0x20006     /* Invalid VTOCE */
#define status_$VTOC_uid_mismatch   0x80020002  /* UID mismatch */
#define status_$no_UID              0x20004     /* No UID found */
#define status_$end_of_file         0x20003     /* End of file */
#define status_$out_of_space        0xF0016     /* Out of disk space */

/*
 * Old format VTOCE structure (0xCC bytes)
 *
 * Used on volumes with old format flag cleared.
 * 5 entries fit in a 1024-byte VTOC block.
 */
typedef struct vtoce_$old_t {
    uint8_t     type_mode;          /* 0x00: Object type and mode flags */
    uint8_t     flags;              /* 0x01: Additional flags */
    int16_t     status;             /* 0x02: Entry status (bit 15 = valid) */
    uint8_t     name[16];           /* 0x04: Object name */
    uid_t       parent_uid;         /* 0x14: Parent directory UID */
    uid_t       dtm;                /* 0x1C: Date/time modified */
    uid_t       acl_uid;            /* 0x24: ACL UID */
    uint32_t    eof_block;          /* 0x28: End of file block */
    uint32_t    current_length;     /* 0x2C: Current length in bytes */
    uint32_t    blocks_used;        /* 0x30: Total blocks used */
    uint16_t    unused_34;          /* 0x34: Unused */
    int16_t     link_count;         /* 0x36: Hard link count (add 1 for actual) */
    uint32_t    dtu;                /* 0x38: Date/time used */
    uint8_t     reserved_3c[0x88];  /* 0x3C: Reserved / file map area */
    uint32_t    fm_direct[8];       /* 0xC4: Direct block pointers */
} vtoce_$old_t;

/*
 * New format VTOCE structure (0x150 bytes)
 *
 * Extended format with ACL UIDs and more metadata.
 * Used on volumes with new format flag set (bit 7 of format byte).
 */
typedef struct vtoce_$new_t {
    uint8_t     type_mode;          /* 0x00: Object type and mode flags */
    uint8_t     flags;              /* 0x01: Additional flags */
    uint8_t     new_flags;          /* 0x02: New format flags */
    uint8_t     reserved_03;        /* 0x03: Reserved */
    uint8_t     name[16];           /* 0x04: Object name */
    uid_t       dtm;                /* 0x14: Date/time modified */
    uid_t       dtu;                /* 0x1C: Date/time used */
    uint16_t    unused_24;          /* 0x24: Unused */
    uint32_t    eof_block;          /* 0x28: End of file block */
    uid_t       dtc;                /* 0x2C: Date/time created (copy of dtm) */
    uid_t       dta;                /* 0x34: Date/time accessed (copy of dtm) */
    uint32_t    current_length;     /* 0x3C: Current length in bytes */
    uint32_t    blocks_used;        /* 0x40: Total blocks used */
    uint32_t    acl_checksum;       /* 0x44: ACL checksum */
    uid_t       owner_uid;          /* 0x48: Owner user UID */
    uid_t       group_uid;          /* 0x50: Group UID */
    uid_t       org_uid;            /* 0x58: Organization UID */
    uint8_t     acl_mode[4];        /* 0x60: ACL mode bytes (0x10 each) */
    uint8_t     acl_flags;          /* 0x64: ACL flags */
    uint8_t     ext_flags;          /* 0x65: Extended flags */
    uint8_t     reserved_66[2];     /* 0x66: Reserved */
    uid_t       acl_uid;            /* 0x68: ACL UID */
    uint16_t    link_count;         /* 0x74: Hard link count (add 1 for actual) */
    uint8_t     reserved_76[0x12];  /* 0x76: Reserved */
    uid_t       parent_uid;         /* 0x88: Parent directory UID */
    uint8_t     reserved_90[0x3c];  /* 0x90: Reserved / file map area */
    uint32_t    fm_direct[8];       /* 0xCC: Direct block pointers (level 1) */
    uint32_t    fm_indirect;        /* 0xEC: Single indirect block (level 2) */
    uint32_t    fm_double;          /* 0xF0: Double indirect block (level 3) */
    uint32_t    fm_triple;          /* 0xF4: Triple indirect block (level 4) */
    /* ... more reserved space to 0x150 ... */
} vtoce_$new_t;

/*
 * VTOC block header (both formats)
 *
 * Each VTOC block begins with a header linking to the next block.
 */
typedef struct vtoc_$block_header_t {
    uint32_t    next_block;         /* 0x00: Next VTOC block in chain (0 = end) */
    int16_t     entry_count;        /* 0x04: Number of valid entries in block */
} vtoc_$block_header_t;

/*
 * VTOC bucket entry (new format)
 *
 * Used for hash-based lookup in new format volumes.
 * Each bucket entry contains 20 UID slots pointing to VTOCEs.
 */
typedef struct vtoc_$bucket_entry_t {
    uint32_t    next_bucket;        /* 0x00: Next bucket in chain */
    uint16_t    slot_index;         /* 0x04: Current slot index */
    uint16_t    reserved;           /* 0x06: Reserved */
    /* 20 entries of 12 bytes each follow:
     * - uid_t uid (8 bytes)
     * - uint32_t block_info (4 bytes)
     */
    struct {
        uid_t       uid;            /* UID of the object */
        uint32_t    block_info;     /* Block location info */
    } slots[VTOCE_BUCKET_SLOTS];    /* 0x08: UID slots */
} vtoc_$bucket_entry_t;

/*
 * Per-volume VTOC data structure
 *
 * Located at vtoc_$data + (vol_idx * 100)
 * Base address: 0xE784D0 (OS_DISK_DATA)
 *
 * Note: Offsets shown are relative to the per-volume base.
 * Negative offsets are at lower addresses.
 */
typedef struct vtoc_$volume_t {
    /* Partition entry table (negative offsets from base) */
    /* -0x54: Hash type (0=UID_$HASH, 2=shift-XOR, 3=simple-XOR) */
    int16_t     hash_type;

    /* -0x52: Hash table size divisor */
    uint16_t    hash_size;

    /* -0x4c: Name directory block 1 */
    uint32_t    name_dir1;

    /* -0x48: Name directory block 2 */
    uint32_t    name_dir2;

    /* -0x44: Current VTOCE location (block << 4 | entry) */
    uint32_t    current_vtoce;

    /* Partition info follows... */
    /* Each partition entry is 6 bytes:
     *   - uint16_t entry_count
     *   - uint32_t start_block
     */
} vtoc_$volume_t;

/*
 * VTOC global data structure
 *
 * Base address: 0xE784D0
 * Mount status at: base + 0x277 + vol_idx
 * Format flag at: base + 0x27f + vol_idx
 */
typedef struct vtoc_$data_t {
    uint8_t     reserved[0x277];    /* 0x000: Per-volume data array */
    int8_t      mounted[8];         /* 0x277: Mount status per volume (0xFF = mounted) */
    int8_t      format[8];          /* 0x27F: Format flag per volume (bit 7 = new format) */
} vtoc_$data_t;

/*
 * External references to VTOC global data
 */
extern vtoc_$data_t vtoc_$data;     /* Base: 0xE784D0 */

/*
 * UID constants for VTOC block types
 * Note: UID_$NIL is declared in base/base.h
 */
extern uid_t VTOC_$UID;             /* 0xE1739C: VTOC block UID */
extern uid_t VTOC_BKT_$UID;         /* 0xE173AC: VTOC bucket UID */

/*
 * Special UIDs for ACL defaults
 */
extern uid_t PPO_$NIL_USER_UID;     /* 0xE174EC: Nil user UID */
extern uid_t RGYC_$G_NIL_UID;       /* 0xE17524: Nil group UID */
extern uid_t PPO_$NIL_ORG_UID;      /* 0xE17574: Nil org UID */

/*
 * Disk data base address
 */
extern uint8_t OS_DISK_DATA[];      /* 0xE784D0: Disk data area base */

/*
 * UID cache structure for quick VTOCE lookup
 *
 * Located at 0xEB2C00
 * 101 buckets, 4 entries per bucket (0x40 bytes per bucket)
 */
#define VTOC_UID_CACHE_BUCKETS  101
#define VTOC_UID_CACHE_ENTRIES  4

typedef struct vtoc_$uid_cache_entry_t {
    uid_t       uid;                /* 0x00: UID */
    uint32_t    block_info;         /* 0x08: Block info (block << 4 | entry) */
    uint16_t    age;                /* 0x0C: Age counter */
    uint16_t    valid;              /* 0x0E: Valid flag (0xFFFF = valid, 0 = invalid) */
} vtoc_$uid_cache_entry_t;

typedef struct vtoc_$uid_cache_bucket_t {
    vtoc_$uid_cache_entry_t entries[VTOC_UID_CACHE_ENTRIES];  /* 16 bytes each */
} vtoc_$uid_cache_bucket_t;

extern vtoc_$uid_cache_bucket_t vtoc_$uid_cache[VTOC_UID_CACHE_BUCKETS];

/*
 * Helper macros
 */

/* Check if volume is mounted */
#define VTOC_IS_MOUNTED(vol_idx) \
    (vtoc_$data.mounted[vol_idx] < 0)

/* Check if volume uses new format */
#define VTOC_IS_NEW_FORMAT(vol_idx) \
    (vtoc_$data.format[vol_idx] < 0)

/* Get per-volume data pointer */
#define VTOC_VOLUME_DATA(vol_idx) \
    ((vtoc_$volume_t *)(OS_DISK_DATA + (vol_idx) * 100))

/* Extract block number from vtoce location */
#define VTOCE_LOC_BLOCK(loc) \
    ((loc) >> 4)

/* Extract entry index from vtoce location */
#define VTOCE_LOC_ENTRY(loc) \
    ((loc) & 0x0F)

/* Build vtoce location from block and entry */
#define VTOCE_LOC_MAKE(block, entry) \
    (((block) << 4) | ((entry) & 0x0F))

/*
 * Internal function prototypes
 */

/* Hash UID to bucket for lookup (FUN_00e383b0) */
void vtoc_$hash_uid(uid_t *uid, short vol_idx, uint16_t *bucket_idx,
                    uint32_t *block, status_$t *status);

/* UID cache lookup/update (FUN_00e38324) */
uint8_t vtoc_$uid_cache_lookup(uid_t *uid, uint16_t *flags, uint32_t *block_info, char update);

/* File map block allocation/traversal (FUN_00e397d0) */
uint16_t vtoc_$fm_traverse(uint32_t *block_ptr, uint16_t level, uint32_t hint);

/* Indirect block freeing helper (FUN_00e39bc2) */
void vtoc_$free_indirect(uint32_t block, uint16_t level, uint32_t limit,
                         uint32_t step, char do_free);

#endif /* VTOC_INTERNAL_H */
