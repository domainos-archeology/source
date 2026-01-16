/*
 * BAT Internal Header
 *
 * Internal data structures and declarations for the Block Allocation Table.
 * This header should only be included by BAT implementation files.
 */

#ifndef BAT_INTERNAL_H
#define BAT_INTERNAL_H

#include "bat/bat.h"
#include "ml/ml.h"
#include "dbuf/dbuf.h"
#include "math/math.h"
#include "time/time.h"
#include "uid/uid.h"

/*
 * Number of partitions per volume
 */
#define BAT_MAX_PARTITIONS  0x83

/*
 * Partition entry structure
 *
 * Each partition tracks its free block count and current VTOCE block.
 * Size: 8 bytes
 */
typedef struct bat_$partition_t {
    uint32_t    free_count;     /* 0x00: Number of free blocks in partition */
    uint8_t     status;         /* 0x04: Partition status:
                                 *       0x01 = active (has VTOCE)
                                 *       0x02 = has free VTOCE entries
                                 */
    uint8_t     vtoce_block[3]; /* 0x05: Current VTOCE block (24-bit) */
} bat_$partition_t;

/*
 * Volume BAT information structure
 *
 * Contains all BAT-related information for a single volume.
 * Size: 0x234 (564) bytes
 *
 * Memory layout at 0xE79244 + (vol_idx * 0x234):
 */
typedef struct bat_$volume_t {
    uint32_t    total_blocks;       /* 0x00: Total blocks on volume */
    uint32_t    free_blocks;        /* 0x04: Number of free blocks */
    uint32_t    bat_block_start;    /* 0x08: Starting disk block for BAT bitmap */
    uint32_t    first_data_block;   /* 0x0C: First usable data block */
    uint16_t    unknown_10;         /* 0x10: Unknown field */
    uint16_t    step_blocks;        /* 0x12: Number of blocks to search before skipping */
    uint16_t    bat_step;           /* 0x14: BAT step value for allocation */
    uint16_t    reserved_pad;       /* 0x16: Padding */
    uint32_t    reserved_blocks;    /* 0x18: Number of reserved blocks */
    uint32_t    unknown_1c;         /* 0x1C: Unknown field */

    /* Partition array starts at offset 0x20 */
    uint16_t    num_partitions;     /* 0x20: Number of partitions */
    uint16_t    partition_start_offset; /* 0x22: Start offset for first partition */
    uint32_t    partition_size;     /* 0x24: Size of each partition (in blocks) */
    uint32_t    alloc_chunk_size;   /* 0x28: Allocation chunk size */
    uint32_t    alloc_chunk_offset; /* 0x2C: Allocation chunk offset */

    /* Partition table - offset 0x30 in volume structure (0x270 relative to partition array base) */
    bat_$partition_t partitions[BAT_MAX_PARTITIONS]; /* 0x30: Partition entries */
} bat_$volume_t;

/*
 * Volume label disk layout (block 0)
 *
 * The volume label is stored in the first block of each volume.
 * This structure maps to offsets used in BAT_$MOUNT/DISMOUNT.
 */
typedef struct bat_$label_t {
    int16_t     version;            /* 0x00: Version (0 = old format, non-0 = new) */
    uint8_t     reserved_02[0x2a];  /* 0x02: Reserved */

    /* Fields at offset 0x2c correspond to bat_$volume_t at 0x00 */
    uint32_t    total_blocks;       /* 0x2C: Total blocks */
    uint32_t    free_blocks;        /* 0x30: Free blocks */
    uint32_t    bat_block_start;    /* 0x34: BAT block start */
    uint32_t    first_data_block;   /* 0x38: First data block */
    uint16_t    unknown_3c;         /* 0x3C: Flags/status field */
    uint16_t    step_blocks;        /* 0x3E: Step blocks */
    uint16_t    bat_step;           /* 0x40: BAT step */
    uint16_t    reserved_42;        /* 0x42: Reserved */
    uint32_t    reserved_blocks;    /* 0x44: Reserved blocks */

    /* Reserved area */
    uint8_t     reserved_48[0x50];  /* 0x48: Reserved */

    /* Timestamp fields */
    uint32_t    mount_time_high;    /* 0xB0: Mount time (high word) */
    uint32_t    mount_time_low;     /* 0xB4: Mount node info */
    uint32_t    boot_time;          /* 0xB8: Boot time */
    uint32_t    dismount_time;      /* 0xBC: Last dismount time */
    uint32_t    current_time;       /* 0xC0: Current time at dismount */

    /* More reserved area */
    uint8_t     reserved_c4[0xa];   /* 0xC4: Reserved */

    int16_t     salvage_flag;       /* 0xCE: Salvage flag (1 = needs salvage) */

    /* Reserved area before partition table */
    uint8_t     reserved_d0[0x2c];  /* 0xD0: Reserved */

    /* Partition info at offset 0xFC - copied to bat_$volume_t.partitions */
    uint16_t    num_partitions;     /* 0xFC: Number of partitions */
    uint16_t    partition_start_offset; /* 0xFE: Partition start offset */
    uint32_t    partition_size;     /* 0x100: Partition size */
    /* ... partition array follows ... */
} bat_$label_t;

/*
 * VTOCE block layout
 *
 * A VTOCE (Volume Table of Contents Entry) block contains file metadata.
 * Each block is 1024 bytes and can hold 3 entries plus header info.
 */
#define VTOCE_MAGIC         0xFEDCA984
#define VTOCE_ENTRIES_PER_BLOCK 3

typedef struct bat_$vtoce_block_t {
    uint32_t    next_vtoce;         /* 0x00: Next VTOCE block in chain */
    int16_t     entry_count;        /* 0x04: Number of entries in use */
    /* ... entry data ... */
    uint8_t     reserved[0x3f2];    /* 0x06: Entry data */
    uint32_t    magic;              /* 0x3F8: Magic number (VTOCE_MAGIC) */
    uint32_t    self_block;         /* 0x3FC: Block number of this VTOCE */
} bat_$vtoce_block_t;

/*
 * Disk info structure for allocation chunk calculation
 *
 * Used in BAT_$MOUNT to calculate allocation parameters from disk geometry.
 * Memory layout at 0xE7A290 + (vol_idx * 0x48):
 */
typedef struct bat_$disk_info_t {
    uint8_t     reserved_00[0x24];  /* 0x00: Reserved */
    uint16_t    sectors_per_track;  /* 0x24: Sectors per track */
    uint8_t     reserved_26[0x10];  /* 0x26: Reserved */
    int16_t     disk_type;          /* 0x36: Disk type (1 = special) */
    uint8_t     reserved_38[0x08];  /* 0x38: Reserved */
    uint32_t    offset;             /* 0x40: Offset value */
} bat_$disk_info_t;

/*
 * Global BAT state
 *
 * These variables track the currently cached BAT bitmap block.
 */

/* Cached BAT bitmap buffer */
extern void     *bat_$cached_buffer;        /* Address: 0xE7A1B0 */

/* Block number of cached BAT bitmap */
extern uint32_t bat_$cached_block;          /* Address: 0xE7A1B4 */

/* Mount status for each volume (0xFF = mounted, 0 = not mounted) */
extern int8_t   bat_$mounted[BAT_MAX_VOLUMES]; /* Address: 0xE7A1BF */

/* Volume flags (high byte contains partition type flag) */
extern uint32_t bat_$volume_flags[BAT_MAX_VOLUMES]; /* Address: 0xE7A1B4 (byte 3 per volume) */

/* Dirty flags for cached buffer */
extern int16_t  bat_$cached_dirty;          /* Address: 0xE7A1C6 */

/* Volume index of cached buffer */
extern int16_t  bat_$cached_vol;            /* Address: 0xE7A1C8 */

/*
 * Volume BAT data array
 * Base address: 0xE79244
 */
extern bat_$volume_t bat_$volumes[BAT_MAX_VOLUMES];

/*
 * Disk info array for allocation calculations
 * Base address: 0xE7A290
 */
extern bat_$disk_info_t bat_$disk_info[BAT_MAX_VOLUMES];

/*
 * UID constants for buffer management
 * Note: LV_LABEL_$UID and NODE_$ME are declared in uid/uid.h
 *       TIME_$* are declared in time/time.h
 */
extern uid_t BAT_$UID;        /* BAT bitmap UID */
extern uid_t VTOC_$UID;       /* VTOCE block UID */

/*
 * Helper macro to get partition VTOCE block as uint32_t
 */
#define BAT_GET_VTOCE_BLOCK(part) \
    ((uint32_t)(part)->vtoce_block[0] << 16 | \
     (uint32_t)(part)->vtoce_block[1] << 8 | \
     (uint32_t)(part)->vtoce_block[2])

#define BAT_SET_VTOCE_BLOCK(part, block) do { \
    (part)->vtoce_block[0] = ((block) >> 16) & 0xFF; \
    (part)->vtoce_block[1] = ((block) >> 8) & 0xFF; \
    (part)->vtoce_block[2] = (block) & 0xFF; \
} while (0)

/*
 * Buffer dirty flag values
 */
#define BAT_BUF_CLEAN       8       /* Buffer is clean, release without write */
#define BAT_BUF_DIRTY       9       /* Buffer is dirty, write on release */
#define BAT_BUF_WRITEBACK   0xB     /* Write back immediately */

#endif /* BAT_INTERNAL_H */
