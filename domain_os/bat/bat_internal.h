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
#include "disk/disk.h"
#include "math/math.h"
#include "time/time.h"
#include "network/network.h"
#include "uid/uid.h"
#include "vtoc/vtoc.h"   /* VTOC_$UID */

/*
 * Partition-table extents.
 *
 * BAT_$MOUNT copies the whole partition table out of the logical-volume
 * label as 0x83 longwords (0x00E3B7F2 move.w #0x82,D6w / 0x00E3B7F8
 * move.l (A2)+,(A4)+ / 0x00E3B7FA dbf), i.e. 0x20C bytes running from
 * bat_$volume_t +0x20 through +0x22B -- one longword short of the two
 * cells at +0x22C and +0x230 that the same routine computes afterwards
 * (0x00E3B838, 0x00E3B874).  BAT_$DISMOUNT copies the same 0x83
 * longwords back (0x00E3B97A / 0x00E3B980).
 *
 * The table's 12-byte header occupies +0x20..+0x2B, so the partition
 * records themselves span +0x2C..+0x22B = 0x200 bytes.  Every accessor
 * scales the partition index by 8 (lsl.l #0x3 at 0x00E3AD74, 0x00E3AE8E,
 * 0x00E3AF40, 0x00E3AF74, 0x00E3AFE2, 0x00E3B380, 0x00E3B3D4,
 * 0x00E3B6B8), so an entry is 8 bytes and the array holds 0x200/8 = 0x40
 * of them.
 */
#define BAT_MAX_PARTITIONS          0x40

/* Longwords BAT_$MOUNT/BAT_$DISMOUNT move for the BAT header (0x00E3B7E2
 * moveq #0x7,D6 and 0x00E3B960 moveq #0x7,D0 -- a dbf loop, so 8). */
#define BAT_HEADER_LONGWORDS        8

/* Longwords BAT_$MOUNT/BAT_$DISMOUNT move for the partition table
 * (0x00E3B7F2 and 0x00E3B97A move.w #0x82 -- a dbf loop, so 0x83). */
#define BAT_PART_TABLE_LONGWORDS    0x83

/*
 * Partition entry structure
 *
 * Each partition tracks its free block count and its current VTOCE block.
 * Size: 8 bytes (index scaled by lsl.l #0x3 at 0x00E3AD74 and friends).
 *
 * Entry i of volume v lives at bat_$volumes[v] + 0x2C + i*8: BAT_$ALLOC_FM
 * reads the free count with `cmp.l (-0x208,A0),D3` at 0x00E3AD86 and the
 * status byte with `move.b (-0x204,A0),D0b` at 0x00E3AD7C, where A0 is the
 * volume record's end (bat_$volumes[v] + 0x234) plus i*8.
 */
typedef struct bat_$partition_t {
    uint32_t    free_count;     /* 0x00: free blocks in this partition.
                                 *   0x00E3AD86, 0x00E3AD96, 0x00E3ADA6,
                                 *   0x00E3AF46, 0x00E3AF86, 0x00E3AF90,
                                 *   0x00E3B386 (subq), 0x00E3B6BE (addq) */
    uint8_t     status;         /* 0x04: high byte of the longword at +0x04:
                                 *       1 = partition holds a VTOCE chain
                                 *           (0x00E3AD80 cmpi.w #0x1)
                                 *       2 = partition has free VTOCE slots
                                 *           (0x00E3AF80 cmpi.w #0x2)
                                 *   BAT_$ADD_PART_VTOCE preserves it with
                                 *   andi.l #-0x1000000 at 0x00E3AE98 */
    uint8_t     vtoce_block[3]; /* 0x05: current VTOCE block, low 24 bits of
                                 *   the same longword (and.l #0xffffff at
                                 *   0x00E3AE94, or.l at 0x00E3AEA4) */
} bat_$partition_t;

_Static_assert(sizeof(bat_$partition_t) == 8,
               "bat_$partition_t stride (lsl.l #0x3 at 0x00E3AD74)");

/*
 * Volume BAT information structure
 *
 * Contains all BAT-related information for a single volume.
 * Size: 0x234 (564) bytes -- `mulu.w #0x234` at 0x00E3AD48, 0x00E3AEF6,
 * 0x00E3AE54, 0x00E3B112, 0x00E3B568, 0x00E3B756, 0x00E3B926, 0x00E3BA26,
 * 0x00E3BA9E, 0x00E3BB20 and 0x00E3BB6A.
 *
 * Addressing: every BAT routine loads A5 with the module data base
 * 0xE79478 (`lea (0xe79478).l,A5`) and then forms
 *
 *     An = A5 + vol_idx * 0x234
 *
 * reaching the record's fields at NEGATIVE displacements from An, so the
 * record for volume v actually begins at 0xE79478 + (v-1) * 0x234.  In C
 * that is modelled by giving bat_$volumes the biased base 0xE79244 and
 * indexing it by vol_idx directly, exactly as the machine does; element 0
 * is the phantom slot the bias creates and is never touched, because
 * BAT_$N_FREE rejects vol_idx 0 (0x00E3BA1A tst.w / 0x00E3BA1E cmpi.w #0x6).
 *
 * The SAU2 link map gives `D E79478 BAT_ size = D54` with BAT_DATA at
 * E79478; the six real records fill E79478..E7A1AF (6 * 0x234 = 0xD38) and
 * the module's scalars follow at E7A1B0.
 */
typedef struct bat_$volume_t {
    /*
     * +0x00..+0x1F: the BAT header, copied verbatim out of the label's
     * +0x2C..+0x4B by BAT_$MOUNT (0x00E3B7DA lea (0x2c,A0),A4 /
     * 0x00E3B7DE lea (-0x234,A1),A2 / moveq #0x7 / move.l (A4)+,(A2)+)
     * and written back by BAT_$DISMOUNT (0x00E3B958-0x00E3B966).
     */
    uint32_t    total_blocks;       /* 0x00: (-0x234,An).  0x00E3B180,
                                     *   0x00E3B23C, 0x00E3B5B8, 0x00E3BA46,
                                     *   cleared by BAT_$DISMOUNT at
                                     *   0x00E3B9C0 */
    uint32_t    free_blocks;        /* 0x04: (-0x230,An).  0x00E3B126,
                                     *   0x00E3B4F4, 0x00E3B5A4, 0x00E3BA42,
                                     *   0x00E3BAB8, 0x00E3BB3A */
    uint32_t    bat_block_start;    /* 0x08: (-0x22c,An); added to
                                     *   block >> 13 to form the BAT bitmap
                                     *   block, 0x00E3B25E and 0x00E3B5E0 */
    uint32_t    first_data_block;   /* 0x0C: (-0x228,An).  0x00E3B170,
                                     *   0x00E3B1A2, 0x00E3B5B2 */
    uint16_t    volume_trouble;     /* 0x10: (-0x224,An); the label's
                                     *   volume_trouble word, copied but not
                                     *   read back out of the record */
    uint16_t    step_blocks;        /* 0x12: (-0x222,An).  BAT_$ALLOCATE
                                     *   reads +0x12 as a LONGWORD
                                     *   (move.l (-0x222,A2),D4 at
                                     *   0x00E3B18C and 0x00E3B396), i.e.
                                     *   step_blocks:bat_step as one value,
                                     *   which is how the label defaults the
                                     *   pair to 3 at 0x00E3B7B2 */
    uint16_t    bat_step;           /* 0x14: (-0x220,An).  BAT_$GET_BAT_STEP
                                     *   returns this word, 0x00E3BB72 */
    uint16_t    reserved_16;        /* 0x16: (-0x21e,An); copied only */
    uint32_t    reserved_blocks;    /* 0x18: (-0x21c,An).  0x00E3B15C,
                                     *   0x00E3B4FC, 0x00E3B59C, 0x00E3B5A8,
                                     *   0x00E3B6B0, 0x00E3BADA, 0x00E3BB2C */
    uint32_t    unknown_1c;         /* 0x1C: (-0x218,An); eighth longword of
                                     *   the header copy, never read */

    /*
     * +0x20..+0x22B: the partition table, copied verbatim out of the
     * label's +0xFC..+0x307 by BAT_$MOUNT as BAT_PART_TABLE_LONGWORDS
     * longwords (0x00E3B7EA-0x00E3B7FA) and written back by BAT_$DISMOUNT
     * (0x00E3B972-0x00E3B982).
     */
    uint16_t    num_partitions;     /* 0x20: (-0x214,An).  Loop bound in
                                     *   BAT_$ALLOC_FM (0x00E3AD58,
                                     *   0x00E3AD66, 0x00E3ADC0),
                                     *   BAT_$ALLOC_VTOCE (0x00E3AF36,
                                     *   0x00E3AF56, 0x00E3AF62) and
                                     *   BAT_$ALLOCATE (0x00E3B3F2); forced
                                     *   to 1 for an old-format volume at
                                     *   0x00E3B80A */
    uint16_t    partition_start_offset; /* 0x22: (-0x212,An); block number of
                                     *   the first partitioned block.
                                     *   0x00E3ADF0, 0x00E3AE62, 0x00E3AF10,
                                     *   0x00E3AFCE, 0x00E3B1A6, 0x00E3B406,
                                     *   0x00E3B668 */
    uint32_t    partition_size;     /* 0x24: (-0x210,An); blocks per
                                     *   partition.  0x00E3AD54, 0x00E3ADE2,
                                     *   0x00E3AE76, 0x00E3AF02, 0x00E3B1AC,
                                     *   0x00E3B678; forced to 0x7FFFFFFF for
                                     *   an old-format volume at 0x00E3B802 */
    uint32_t    unknown_28;         /* 0x28: (-0x20c,An); third longword of
                                     *   the partition-table header.  Copied
                                     *   in both directions, never read by
                                     *   any BAT routine. */

    bat_$partition_t partitions[BAT_MAX_PARTITIONS]; /* 0x2C..0x22B */

    /*
     * +0x22C..+0x233: computed by BAT_$MOUNT from the drive's geometry
     * (0x00E3B81E-0x00E3B874); NOT part of either copy.
     */
    uint32_t    alloc_chunk_size;   /* 0x22C: (-0x8,An); blocks per
                                     *   allocation chunk (a track).  Set at
                                     *   0x00E3B838/0x00E3B854, read by
                                     *   BAT_$ALLOCATE at 0x00E3B212,
                                     *   0x00E3B220, 0x00E3B234, 0x00E3B43E,
                                     *   0x00E3B44C, 0x00E3B478 */
    uint32_t    alloc_chunk_offset; /* 0x230: (-0x4,An); volume-relative
                                     *   block at which the first whole chunk
                                     *   begins.  Set at 0x00E3B874, read by
                                     *   BAT_$ALLOCATE at 0x00E3B1FC,
                                     *   0x00E3B204, 0x00E3B20E, 0x00E3B22E,
                                     *   0x00E3B432, 0x00E3B43A, 0x00E3B45A,
                                     *   0x00E3B46A */
} bat_$volume_t;

_Static_assert(sizeof(bat_$volume_t) == 0x234,
               "bat_$volume_t stride (mulu.w #0x234 at 0x00E3B756)");
_Static_assert(__builtin_offsetof(bat_$volume_t, total_blocks) == 0x000,
               "bat_$volume_t.total_blocks ((-0x234,An), 0x00E3B180)");
_Static_assert(__builtin_offsetof(bat_$volume_t, free_blocks) == 0x004,
               "bat_$volume_t.free_blocks ((-0x230,An), 0x00E3BA42)");
_Static_assert(__builtin_offsetof(bat_$volume_t, bat_block_start) == 0x008,
               "bat_$volume_t.bat_block_start ((-0x22c,An), 0x00E3B25E)");
_Static_assert(__builtin_offsetof(bat_$volume_t, first_data_block) == 0x00C,
               "bat_$volume_t.first_data_block ((-0x228,An), 0x00E3B170)");
_Static_assert(__builtin_offsetof(bat_$volume_t, volume_trouble) == 0x010,
               "bat_$volume_t.volume_trouble ((-0x224,An), 0x00E3B7DA copy)");
_Static_assert(__builtin_offsetof(bat_$volume_t, step_blocks) == 0x012,
               "bat_$volume_t.step_blocks ((-0x222,An), 0x00E3B18C)");
_Static_assert(__builtin_offsetof(bat_$volume_t, bat_step) == 0x014,
               "bat_$volume_t.bat_step ((-0x220,An), 0x00E3BB72)");
_Static_assert(__builtin_offsetof(bat_$volume_t, reserved_blocks) == 0x018,
               "bat_$volume_t.reserved_blocks ((-0x21c,An), 0x00E3BB2C)");
_Static_assert(__builtin_offsetof(bat_$volume_t, unknown_1c) == 0x01C,
               "bat_$volume_t.unknown_1c ((-0x218,An), 0x00E3B7DA copy)");
_Static_assert(__builtin_offsetof(bat_$volume_t, num_partitions) == 0x020,
               "bat_$volume_t.num_partitions ((-0x214,An), 0x00E3AD58)");
_Static_assert(__builtin_offsetof(bat_$volume_t, partition_start_offset) == 0x022,
               "bat_$volume_t.partition_start_offset ((-0x212,An), 0x00E3AE62)");
_Static_assert(__builtin_offsetof(bat_$volume_t, partition_size) == 0x024,
               "bat_$volume_t.partition_size ((-0x210,An), 0x00E3AD54)");
_Static_assert(__builtin_offsetof(bat_$volume_t, unknown_28) == 0x028,
               "bat_$volume_t.unknown_28 ((-0x20c,An), copy only)");
_Static_assert(__builtin_offsetof(bat_$volume_t, partitions) == 0x02C,
               "bat_$volume_t.partitions[0].free_count ((-0x208,An), 0x00E3B810)");
_Static_assert(__builtin_offsetof(bat_$volume_t, partitions[0].status) == 0x030,
               "bat_$volume_t.partitions[0].status ((-0x204,An), 0x00E3AD7C)");
_Static_assert(__builtin_offsetof(bat_$volume_t, alloc_chunk_size) == 0x22C,
               "bat_$volume_t.alloc_chunk_size ((-0x8,An), 0x00E3B838)");
_Static_assert(__builtin_offsetof(bat_$volume_t, alloc_chunk_offset) == 0x230,
               "bat_$volume_t.alloc_chunk_offset ((-0x4,An), 0x00E3B874)");

/* The partition table copy must end exactly where alloc_chunk_size begins. */
_Static_assert(__builtin_offsetof(bat_$volume_t, num_partitions) +
               BAT_PART_TABLE_LONGWORDS * 4 ==
               __builtin_offsetof(bat_$volume_t, alloc_chunk_size),
               "partition-table copy extent (0x00E3B7F2 move.w #0x82)");

/*
 * The logical-volume label record (bat_$label_t) is defined in
 * bat/bat.h: DISK_$LV_MOUNT (0x00E6CA3A) reads the same block, and
 * disk/ is outside this subsystem (bead source-f5j9).
 */

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
