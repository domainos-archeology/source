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

/*
 * The (step_blocks, bat_step) pair at +0x12 is read and written as ONE
 * longword by the routines that set the allocation stride:
 *
 *   0x00E3B18C  move.l (-0x222,A2),D4          ; BAT_$ALLOCATE, initial value
 *   0x00E3B396  move.l (-0x222,A2),(-0xc,A6)   ; BAT_$ALLOCATE, reload
 *   0x00E3B7AA  tst.l (0x3e,A0)                ; BAT_$MOUNT, label copy
 *   0x00E3B7B2  move.l D6,(0x3e,A0)            ; BAT_$MOUNT, default = 3
 *
 * BAT_$GET_BAT_STEP reads only its low half (move.w (-0x220,A1) at
 * 0x00E3BB72), which on big-endian m68k is bat_step.  Spelled as a
 * shift/or so the tree neither assumes m68k byte order nor takes an
 * unaligned longword access at an offset that is only word aligned.
 */
#define BAT_STEP_LONG(vol) \
    (((uint32_t)(vol)->step_blocks << 16) | (uint32_t)(vol)->bat_step)

#define BAT_LABEL_STEP_LONG(label) \
    (((uint32_t)(label)->step_blocks << 16) | (uint32_t)(label)->bat_step)

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
 * BAT_$MOUNT's view of the DISK_$DVTBL entry for a volume is disk_$volume_t
 * (disk/disk.h), reached through DISK_VOL(vol_idx).  0x00E3B820-0x00E3B830
 *
 *   0x00E3B820  movea.l #0xe7a290,A2    ; DISK_$DVTBL
 *   0x00E3B826  lsl.w #0x3,D6w          ; vol*8
 *   0x00E3B828  move.w D6w,D7w
 *   0x00E3B82A  lsl.w #0x3,D7w          ; vol*64
 *   0x00E3B82C  add.w D7w,D6w           ; vol*0x48
 *   0x00E3B830  lea (0x0,A2,D6w*0x1),A2 ; A2 = 0xE7A290 + vol*0x48
 *
 * then reads the fields at NEGATIVE displacements, so the record for volume
 * v starts at A2 - 0x48 = 0xE7A248 + v*0x48 -- exactly the biased base
 * DISK_VOL(v) uses (DISK_VOLUME_BASE 0xE7A1CC + DISK_VOL_DESC_OFFSET 0x7C +
 * v*0x48).  The phantom element 0 sits at 0xE7A248 and DISK_$DVTBL is
 * element 1.
 */

/*
 * The BAT module data segment (SAU2 map: `D E79478  BAT_  size = D54`, one
 * interior symbol `E79478  BAT_DATA`).
 *
 * Every BAT routine loads A5 = 0xE79478 and reaches the six volume records at
 * negative displacements from A5 + vol_idx*0x234, so the record for volume v
 * starts at 0xE79244 + v*0x234 and volume indices run 1..6 (BAT_$N_FREE
 * rejects 0 and >6 at 0x00E3BA1A / 0x00E3BA1E).  Six records fill
 * 0xE79478..0xE7A1AF and the module scalars follow at 0xE7A1B0, which is what
 * pins the count.  The tree models the bias by giving both the volume array
 * and the two per-volume byte arrays a phantom element 0.
 *
 * Scalar cells, all reached as (disp,A5) with A5 = 0xE79478 (the disassembly
 * displacement is the address minus 0xE79478):
 *
 *   0xE7A1B0  (0xd38)  long   bat_$cached_buffer
 *   0xE7A1B4  (0xd3c)  long   bat_$cached_block
 *   0xE7A1B7  (0xd3f)  byte[] bat_$volume_flags, biased; [1..6] = B8..BD
 *   0xE7A1BE           2 bytes, never referenced (alignment)
 *   0xE7A1BF  (0xd47)  byte[] bat_$mounted, biased; [1..6] = C0..C5
 *   0xE7A1C6  (0xd4e)  word   bat_$cached_dirty
 *   0xE7A1C8  (0xd50)  word   bat_$cached_vol
 *   0xE7A1CA           2 bytes, never referenced (segment tail)
 */
#define BAT_DATA_BASE               0x00e79478u  /* map: D E79478 BAT_ */
#define BAT_DATA_SIZE               0x00000d54u  /* map: size = D54 */
#define BAT_DATA_END                (BAT_DATA_BASE + BAT_DATA_SIZE)

/* Highest volume index (0x00E3BA1E cmpi.w #0x6) */
#define BAT_MAX_VOL_INDEX           6

/* Biased base of bat_$volumes: element 0 is a phantom before the segment. */
#define BAT_VOLUMES_BASE            0x00e79244u

#define BAT_CACHED_BUFFER_ADDR      0x00e7a1b0u  /* (0xd38,A5) */
#define BAT_CACHED_BLOCK_ADDR       0x00e7a1b4u  /* (0xd3c,A5) */
#define BAT_VOLUME_FLAGS_BASE       0x00e7a1b7u  /* (0xd3f,A5), biased */
#define BAT_MOUNTED_BASE            0x00e7a1bfu  /* (0xd47,A5), biased */
#define BAT_CACHED_DIRTY_ADDR       0x00e7a1c6u  /* (0xd4e,A5) */
#define BAT_CACHED_VOL_ADDR         0x00e7a1c8u  /* (0xd50,A5) */

/* The six volume records end exactly where the scalars begin. */
_Static_assert(BAT_VOLUMES_BASE + 0x234u == BAT_DATA_BASE,
               "bat_$volumes[1] is the start of the BAT_ segment");
_Static_assert(BAT_VOLUMES_BASE + (BAT_MAX_VOL_INDEX + 1) * 0x234u ==
               BAT_CACHED_BUFFER_ADDR,
               "six 0x234-byte volume records must fill 0xE79478..0xE7A1AF");
_Static_assert(BAT_DATA_BASE + BAT_MAX_VOL_INDEX * 0x234u ==
               BAT_CACHED_BUFFER_ADDR,
               "the BAT scalars must start at 0xE7A1B0");

/* Scalar chain: each cell's extent must reach the next cell. */
_Static_assert(BAT_CACHED_BUFFER_ADDR + 4u == BAT_CACHED_BLOCK_ADDR,
               "bat_$cached_buffer is one longword (0x00E3B2E2 move.l A0)");
_Static_assert(BAT_CACHED_BLOCK_ADDR + 4u == BAT_VOLUME_FLAGS_BASE + 1u,
               "bat_$volume_flags[1] follows bat_$cached_block at 0xE7A1B8");
_Static_assert(BAT_VOLUME_FLAGS_BASE + 1u + BAT_MAX_VOL_INDEX + 2u ==
               BAT_MOUNTED_BASE + 1u,
               "two unreferenced bytes separate the flag and mount arrays");
_Static_assert(BAT_MOUNTED_BASE + 1u + BAT_MAX_VOL_INDEX ==
               BAT_CACHED_DIRTY_ADDR,
               "bat_$mounted[6] ends where bat_$cached_dirty begins");
_Static_assert(BAT_CACHED_DIRTY_ADDR + 2u == BAT_CACHED_VOL_ADDR,
               "bat_$cached_dirty is one word (0x00E3B304 move.w #0x8)");
_Static_assert(BAT_CACHED_VOL_ADDR + 2u + 2u == BAT_DATA_END,
               "two unreferenced bytes end the BAT_ segment at 0xE7A1CC");

/*
 * Global BAT state
 *
 * These variables track the currently cached BAT bitmap block.
 */

/* Cached BAT bitmap buffer (0xE7A1B0) */
extern void     *bat_$cached_buffer;

/* Block number of the cached BAT bitmap (0xE7A1B4) */
extern uint32_t bat_$cached_block;

/*
 * Per-volume new-format flag (biased base 0xE7A1B7, entries 1..6).
 *
 * BAT_$MOUNT stores `sne` of the label version word into it
 * (0x00E3B760-0x00E3B764), so an entry is 0 for an old-format volume and
 * -1 for a new-format one, and every reader tests only its sign
 * (0x00E3B14A, 0x00E3B768, 0x00E3B96C, 0x00E3BAC2 `tst.b` + `bpl`).
 */
extern int8_t   bat_$volume_flags[BAT_MAX_VOLUMES];

/*
 * Per-volume mount flag (biased base 0xE7A1BF, entries 1..6).
 * 0xFF = mounted (0x00E3B792 `st`), 0 = not mounted (0x00E3B72A `clr.b`).
 */
extern int8_t   bat_$mounted[BAT_MAX_VOLUMES];

/* Dirty state passed to DBUF_$SET_BUFF for the cached buffer (0xE7A1C6) */
extern int16_t  bat_$cached_dirty;

/* Volume index owning the cached buffer (0xE7A1C8) */
extern int16_t  bat_$cached_vol;

/*
 * Volume BAT data array (biased base 0xE79244, entries 1..6 at
 * 0xE79478..0xE7A1AF)
 */
extern bat_$volume_t bat_$volumes[BAT_MAX_VOLUMES];

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
 * The partition record's second longword (entry +0x04) as one value:
 * status in the high byte, VTOCE block in the low 24 bits.
 *
 * BAT_$ALLOC_VTOCE rewrites the chain head with an UNMASKED or:
 *
 *   0x00E3B08E  andi.l #-0x1000000,(-0x204,A1)   ; keep the status byte
 *   0x00E3B096  move.l (A3),D0                   ; the new block, unmasked
 *   0x00E3B098  or.l D0,(-0x204,A1)
 *
 * BAT_$ADD_PART_VTOCE (0x00E3AE86-0x00E3AEA4) does the same: the
 * `and.l (-0x204,A0),D2` at 0x00E3AE94 masks the OLD chain value for the
 * function result, then `andi.l #-0x1000000` / `or.l (0xa,A6)` ORs the new
 * block in unmasked.  The two routines do not differ.
 */
#define BAT_PART_CHAIN_LONG(part) \
    (((uint32_t)(part)->status << 24) | BAT_GET_VTOCE_BLOCK(part))

#define BAT_SET_PART_CHAIN_LONG(part, value) do { \
    uint32_t _v = (uint32_t)(value); \
    (part)->status = (uint8_t)(_v >> 24); \
    BAT_SET_VTOCE_BLOCK((part), _v); \
} while (0)

/*
 * Buffer dirty flag values
 */
#define BAT_BUF_CLEAN       8       /* Buffer is clean, release without write */
#define BAT_BUF_DIRTY       9       /* Buffer is dirty, write on release */
#define BAT_BUF_WRITEBACK   0xB     /* Write back immediately */

#endif /* BAT_INTERNAL_H */
