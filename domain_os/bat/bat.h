/*
 * BAT - Block Allocation Table
 *
 * This module manages disk block allocation for Domain/OS volumes.
 * The BAT uses a bitmap to track free/allocated blocks, with partitions
 * to organize the disk space and VTOCE (Volume Table of Contents Entry)
 * allocation for file metadata.
 *
 * Each volume can have up to 0x83 (131) partitions, with each partition
 * tracking its own free block count and VTOCE chain.
 */

#ifndef BAT_H
#define BAT_H

#include "base/base.h"

/*
 * BAT UID - Block Allocation Table unique identifier
 * Address: 0xE173A4
 */
extern uid_t BAT_$UID;

/*
 * BAT Lock ID - Used with ML_$LOCK/ML_$UNLOCK
 */
#define ML_LOCK_BAT     0x11

/*
 * Maximum number of volumes supported
 */
#define BAT_MAX_VOLUMES     7

/*
 * Status codes
 */
#define bat_$not_mounted            0x10004
#define bat_$invalid_block          0x80010003
#define bat_$error                  0x80010001

/*
 * bat_$label_t - the logical-volume label, block 0 of a logical volume
 *
 * The BAT manager owns this record: BAT_$MOUNT (0x00E3B6F8) reads it and
 * BAT_$DISMOUNT (0x00E3B8BE) writes it back, both through
 * DBUF_$GET_BLOCK(vol_idx, 0, LV_LABEL_$UID, ...).  DISK_$LV_MOUNT
 * (0x00E6CA3A) reads the same block through DISK_$GET_BLOCK, which is why
 * the record lives in the public header rather than in bat_internal.h.
 *
 * Field names come from AEGIS Internals and Data Structures (Jan 1986),
 * Figure 4-4 "Logical Volume Label Format" (section 4.3.1) and Figure 4-5
 * "Relationship of BAT header and BAT" (section 4.3.3), which lists the BAT
 * header's cells in the order the label stores them:
 *
 *   00  Version | Unused
 *   04  Logical Volume Name
 *   24  Logical Volume ID
 *   2C  BAT Header:  2C No. of Blocks Represented
 *                    30 No. Blocks Free
 *                    34 DADDR of first BAT Block
 *                    38 Blk No. represented by First BAT Bit
 *                    3C Volume Trouble | Unused
 *                    40 BAT Step to use on This Volume
 *   4C  VTOC Header (100 bytes)
 *   B0  Time LV Label Written
 *   B4  Unused | Last Mounted Node
 *   B8  Time System Booted
 *   BC  Time Volume Dismounted
 *
 * The SAU2 link map names no symbol for this record or its cells; the only
 * label symbols it carries are the two canned UIDs PV_LABEL_$UID (0xE1738C)
 * and LV_LABEL_$UID (0xE17394), so the layout below rests on the three
 * routines cited per field.
 */
typedef struct bat_$label_t {
    int16_t     version;            /* 0x00: 0 = old format, non-0 = new.
                                     *   BAT_$MOUNT tst.w (A0) 0x00E3B760;
                                     *   DISK_$LV_MOUNT cmpi.w #0x1,(A2)
                                     *   0x00E6CB80 */
    uint16_t    unused_02;          /* 0x02: "Unused" (fig. 4-4) */
    char        lv_name[0x20];      /* 0x04: "Logical Volume Name" (fig. 4-4) */
    uid_t       lv_uid;             /* 0x24: "Logical Volume ID" (fig. 4-4).
                                     *   DISK_$LV_MOUNT compares both
                                     *   longwords, lea (0x24,A2),A0 then two
                                     *   cmpm.l at 0x00E6CB88-0x00E6CB98 */

    /*
     * 0x2C..0x4B: the BAT header.  BAT_$MOUNT copies these eight longwords
     * into bat_$volume_t +0x00 (lea (0x2c,A0),A4 / dbf #7, 0x00E3B7DA);
     * BAT_$DISMOUNT copies them back (0x00E3B95C).
     */
    uint32_t    total_blocks;       /* 0x2C: "No. of Blocks Represented".
                                     *   DISK_$LV_MOUNT adds it to
                                     *   first_data_block, add.l (0x2c,A1),D1
                                     *   at 0x00E6CBCC */
    uint32_t    free_blocks;        /* 0x30: "No. Blocks Free" */
    uint32_t    bat_block_start;    /* 0x34: "DADDR of first BAT Block" */
    uint32_t    first_data_block;   /* 0x38: "Blk No. represented by First BAT
                                     *   Bit".  DISK_$LV_MOUNT reads it with
                                     *   move.l (0x38,A1),D1 at 0x00E6CBC8 */
    uint16_t    volume_trouble;     /* 0x3C: "Volume Trouble" (fig. 4-5).
                                     *   BAT_$MOUNT tests bit 12 on a new
                                     *   format volume and the sign bit on an
                                     *   old one (btst.l #0xc / smi at
                                     *   0x00E3B76E-0x00E3B77E) and rewrites
                                     *   bit 4 (andi.b #-0x11 / or.b at
                                     *   0x00E3B79C-0x00E3B7A6) */
    uint16_t    step_blocks;        /* 0x3E: "Unused" in fig. 4-5.  BAT_$MOUNT
                                     *   tests it together with bat_step as one
                                     *   longword and defaults the pair to 3
                                     *   (tst.l (0x3e,A0) 0x00E3B7AA) */
    uint16_t    bat_step;           /* 0x40: "BAT Step to use on This Volume".
                                     *   DISK_$LV_MOUNT copies it into the LV
                                     *   and PV descriptors,
                                     *   move.w (0x40,A1),(-0x22,A0) at
                                     *   0x00E6CBEA */
    uint16_t    reserved_42;        /* 0x42 */
    uint32_t    reserved_blocks;    /* 0x44 */
    uint32_t    unknown_48;         /* 0x48: eighth longword of the copied
                                     *   block; reaches bat_$volume_t +0x1C */

    uint8_t     vtoc_header[0x64];  /* 0x4C: "VTOC Header (100 Bytes)"
                                     *   (fig. 4-4); the VTOC manager's
                                     *   record, untouched by BAT */

    uint32_t    mount_time_high;    /* 0xB0: "Time LV Label Written".
                                     *   move.l D4,(0xb0,A0) 0x00E3B796 and
                                     *   0x00E3B98C */
    uint32_t    mount_time_low;     /* 0xB4: "Unused | Last Mounted Node".
                                     *   BAT_$MOUNT keeps the top 12 bits and
                                     *   ORs in NODE_$ME & 0xFFFFF,
                                     *   andi.l #-0x100000,(0xb4,A0) at
                                     *   0x00E3B7B6 */
    uint32_t    boot_time;          /* 0xB8: "Time System Booted".
                                     *   move.l (0xe2b0ec),(0xb8,A0)
                                     *   0x00E3B7C8 */
    uint32_t    dismount_time;      /* 0xBC: "Time Volume Dismounted".
                                     *   move.l D4,(0xbc,A0) 0x00E3B7D0 */
    uint32_t    current_time;       /* 0xC0: BAT_$DISMOUNT stamps the current
                                     *   clock here, move.l D0,(0xc0,A0) at
                                     *   0x00E3B990.  Fig. 4-4's name for this
                                     *   cell does not fit that use, so it is
                                     *   left with the tree's own name. */

    uint8_t     reserved_c4[0xa];   /* 0xC4 */

    int16_t     salvage_flag;       /* 0xCE: 1 = needs salvage.  BAT_$MOUNT
                                     *   cmpi.w #0x1,(0xce,A0) 0x00E3B780 and
                                     *   move.w #0x1,(0xce,A0) 0x00E3B7D4;
                                     *   BAT_$DISMOUNT clr.w (0xce,A0)
                                     *   0x00E3B994 */

    uint8_t     reserved_d0[0x2c];  /* 0xD0 */

    /*
     * 0xFC: BAT_$MOUNT copies 0x83 longwords from here into bat_$volume_t
     * +0x20 (lea (0xfc,A0),A2 / move.w #0x82,D6w / dbf at
     * 0x00E3B7EA-0x00E3B7FA); BAT_$DISMOUNT copies them back (0x00E3B976).
     */
    uint16_t    num_partitions;         /* 0xFC  -> bat_$volume_t +0x20 */
    uint16_t    partition_start_offset; /* 0xFE  -> bat_$volume_t +0x22 */
    uint32_t    partition_size;         /* 0x100 -> bat_$volume_t +0x24 */
    uint32_t    unknown_104;            /* 0x104 -> bat_$volume_t +0x28 */

    /*
     * 0x108: the 0x40 eight-byte partition records, mirroring
     * bat_$volume_t.partitions.  The copy at 0x00E3B7EA moves
     * 0x83 longwords starting at 0xFC, so it ends at 0x308; the
     * 0xC-byte header above leaves exactly 0x200 bytes here.
     * Held as raw bytes because bat_$partition_t is internal to
     * the BAT manager (bat/bat_internal.h).
     */
    uint8_t     partition_table[0x200]; /* 0x108..0x307 */
} bat_$label_t;

/* Every field the BAT and DISK code touches, at the address that proves it. */
_Static_assert(__builtin_offsetof(bat_$label_t, version) == 0x00,
               "bat_$label_t.version (0x00E3B760, 0x00E6CB80)");
_Static_assert(__builtin_offsetof(bat_$label_t, lv_name) == 0x04,
               "bat_$label_t.lv_name (AEGIS Internals fig. 4-4)");
_Static_assert(__builtin_offsetof(bat_$label_t, lv_uid) == 0x24,
               "bat_$label_t.lv_uid (0x00E6CB88)");
_Static_assert(__builtin_offsetof(bat_$label_t, total_blocks) == 0x2C,
               "bat_$label_t.total_blocks (0x00E3B7DA, 0x00E6CBCC)");
_Static_assert(__builtin_offsetof(bat_$label_t, free_blocks) == 0x30,
               "bat_$label_t.free_blocks (0x00E3B7DA)");
_Static_assert(__builtin_offsetof(bat_$label_t, bat_block_start) == 0x34,
               "bat_$label_t.bat_block_start (0x00E3B7DA)");
_Static_assert(__builtin_offsetof(bat_$label_t, first_data_block) == 0x38,
               "bat_$label_t.first_data_block (0x00E3B7DA, 0x00E6CBC8)");
_Static_assert(__builtin_offsetof(bat_$label_t, volume_trouble) == 0x3C,
               "bat_$label_t.volume_trouble (0x00E3B76E, 0x00E3B79C)");
_Static_assert(__builtin_offsetof(bat_$label_t, step_blocks) == 0x3E,
               "bat_$label_t.step_blocks (0x00E3B7AA)");
_Static_assert(__builtin_offsetof(bat_$label_t, bat_step) == 0x40,
               "bat_$label_t.bat_step (0x00E3B7AA, 0x00E6CBEA)");
_Static_assert(__builtin_offsetof(bat_$label_t, reserved_blocks) == 0x44,
               "bat_$label_t.reserved_blocks (0x00E3B7DA)");
_Static_assert(__builtin_offsetof(bat_$label_t, unknown_48) == 0x48,
               "bat_$label_t.unknown_48 (0x00E3B7DA)");
_Static_assert(__builtin_offsetof(bat_$label_t, mount_time_high) == 0xB0,
               "bat_$label_t.mount_time_high (0x00E3B796, 0x00E3B98C)");
_Static_assert(__builtin_offsetof(bat_$label_t, mount_time_low) == 0xB4,
               "bat_$label_t.mount_time_low (0x00E3B7B6)");
_Static_assert(__builtin_offsetof(bat_$label_t, boot_time) == 0xB8,
               "bat_$label_t.boot_time (0x00E3B7C8)");
_Static_assert(__builtin_offsetof(bat_$label_t, dismount_time) == 0xBC,
               "bat_$label_t.dismount_time (0x00E3B7D0)");
_Static_assert(__builtin_offsetof(bat_$label_t, current_time) == 0xC0,
               "bat_$label_t.current_time (0x00E3B990)");
_Static_assert(__builtin_offsetof(bat_$label_t, salvage_flag) == 0xCE,
               "bat_$label_t.salvage_flag (0x00E3B780, 0x00E3B994)");
_Static_assert(__builtin_offsetof(bat_$label_t, num_partitions) == 0xFC,
               "bat_$label_t.num_partitions (0x00E3B7EA, 0x00E3B976)");
_Static_assert(__builtin_offsetof(bat_$label_t, partition_start_offset) == 0xFE,
               "bat_$label_t.partition_start_offset (0x00E3B7EA)");
_Static_assert(__builtin_offsetof(bat_$label_t, partition_size) == 0x100,
               "bat_$label_t.partition_size (0x00E3B7EA)");
_Static_assert(__builtin_offsetof(bat_$label_t, unknown_104) == 0x104,
               "bat_$label_t.unknown_104 (0x00E3B7EA copy)");
_Static_assert(__builtin_offsetof(bat_$label_t, partition_table) == 0x108,
               "bat_$label_t.partition_table (0x00E3B7EA copy)");
/* The 0x83-longword copy at 0x00E3B7EA / 0x00E3B980 starts at 0xFC and must
 * land wholly inside the record. */
_Static_assert(sizeof(bat_$label_t) >=
               __builtin_offsetof(bat_$label_t, num_partitions) + 0x83 * 4,
               "bat_$label_t partition-table copy extent (0x00E3B7F2)");

/*
 * BAT_$ALLOCATE - Allocate disk blocks
 *
 * Allocates blocks from the volume's free (or reserved) block pool.
 * Searches the BAT bitmap starting near the hint block.
 *
 * alloc_count and use_reserved are two separate 16-bit parameters, at
 * (0x0e,A6) and (0x10,A6): 0x00E3B120 tests the second on its own with
 * `tst.w` and 0x00E3B38E compares the first on its own with `cmp.w`.  A
 * caller that pushes them with one `move.l #0x10000` is passing
 * alloc_count 1 and use_reserved 0, because on big-endian m68k the high
 * half of that longword lands at 0x0e.
 *
 * @param vol_idx      Volume index (1-6)
 * @param hint         Hint block number for locality
 * @param alloc_count  Number of blocks to allocate
 * @param use_reserved 0 = free pool, non-zero = reserved pool
 * @param blocks_out   Output array receiving allocated block numbers
 * @param status       Output status code
 */
void BAT_$ALLOCATE(int16_t vol_idx, uint32_t hint, int16_t alloc_count,
                   int16_t use_reserved, uint32_t *blocks_out,
                   status_$t *status);

/*
 * BAT_$FREE - Free disk blocks
 *
 * Returns blocks to the volume's free block pool.
 *
 * @param blocks     Array of block addresses to free
 * @param count      Number of blocks to free
 * @param vol_idx    Volume index (0-6)
 * @param reserved   If non-zero, return blocks to reserved pool
 * @param status     Output status code
 */
void BAT_$FREE(uint32_t *blocks, int16_t count, int16_t vol_idx,
               int16_t reserved, status_$t *status);

/*
 * BAT_$ALLOC_FM - Allocate a block using First Match algorithm
 *
 * Allocates a single block by searching partitions for available space.
 * Uses a first-match strategy starting from the middle partition.
 *
 * @param vol_idx    Volume index (0-6)
 * @param status     Output status code
 *
 * @return           Allocated block number, or 0 on failure
 */
uint32_t BAT_$ALLOC_FM(int16_t vol_idx, status_$t *status);

/*
 * BAT_$ALLOC_VTOCE - Allocate a VTOCE block
 *
 * Allocates a Volume Table of Contents Entry block for file metadata.
 * May create a new VTOCE block if existing ones are full.
 *
 * @param vol_idx      Volume index (0-6)
 * @param hint         Hint block for locality (or 0 for any)
 * @param block_out    Output receiving allocated VTOCE block number
 * @param status       Output status code
 * @param new_vtoce    Output: 0xFF if a new VTOCE block was allocated
 *
 * @return             The locked DBUF buffer holding the VTOCE block
 *                     (returned in A0 at 0xE3B0C8), or NULL on failure.
 *                     The caller must release it with DBUF_$SET_BUFF.
 */
void *BAT_$ALLOC_VTOCE(int16_t vol_idx, uint32_t hint, uint32_t *block_out,
                       status_$t *status, int8_t *new_vtoce);

/*
 * BAT_$ADD_PART_VTOCE - Add VTOCE to partition chain
 *
 * Updates the partition's VTOCE chain with a new or updated block.
 *
 * @param vol_idx    Volume index (0-6)
 * @param block      VTOCE block number
 * @param status     Output status code
 *
 * @return           Previous VTOCE block in chain
 */
uint32_t BAT_$ADD_PART_VTOCE(int16_t vol_idx, uint32_t block, status_$t *status);

/*
 * BAT_$MOUNT - Mount a volume's BAT
 *
 * Initializes the BAT data structures for a volume by reading the
 * volume label and partition information from disk.
 *
 * @param vol_idx    Volume index (0-6)
 * @param salvage_ok If negative, skip salvage check
 * @param status     Output status code
 */
void BAT_$MOUNT(int16_t vol_idx, int8_t salvage_ok, status_$t *status);

/*
 * BAT_$DISMOUNT - Dismount a volume's BAT
 *
 * Flushes and releases BAT data structures for a volume.
 * Updates the volume label with current statistics if requested.
 *
 * @param vol_idx    Volume index (0-6)
 * @param flags      If negative, don't write label; otherwise write updated stats
 * @param status     Output status code
 */
void BAT_$DISMOUNT(int16_t vol_idx, int16_t flags, status_$t *status);

/*
 * BAT_$N_FREE - Get free block count
 *
 * Returns the number of free and total blocks on a volume.
 *
 * @param vol_idx_ptr  Pointer to volume index
 * @param free_out     Output receiving free block count
 * @param total_out    Output receiving total block count
 * @param status       Output status code
 */
void BAT_$N_FREE(uint16_t *vol_idx_ptr, uint32_t *free_out, uint32_t *total_out,
                 status_$t *status);

/*
 * BAT_$RESERVE - Reserve blocks for future allocation
 *
 * Moves blocks from the free pool to the reserved pool.
 * Reserved blocks can only be allocated with the reserved flag.
 *
 * @param vol_idx    Volume index (0-6)
 * @param count      Number of blocks to reserve
 * @param status     Output status code
 */
void BAT_$RESERVE(int16_t vol_idx, uint32_t count, status_$t *status);

/*
 * BAT_$CANCEL - Cancel reserved blocks
 *
 * Moves blocks from the reserved pool back to the free pool.
 *
 * @param vol_idx    Volume index (0-6)
 * @param count      Number of blocks to cancel
 * @param status     Output status code
 */
void BAT_$CANCEL(int16_t vol_idx, uint32_t count, status_$t *status);

/*
 * BAT_$GET_BAT_STEP - Get BAT allocation step
 *
 * Returns the BAT step value for a volume, which controls
 * block allocation locality.
 *
 * @param vol_idx    Volume index (0-6)
 *
 * @return           BAT step value
 */
uint16_t BAT_$GET_BAT_STEP(int16_t vol_idx);

#endif /* BAT_H */
