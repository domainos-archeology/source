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
#include "route/route.h"
#include "network/network.h"
#include "netlog/netlog.h"
#include "rgyc/rgyc.h"   /* RGYC_$G_NIL_UID */

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
/* status_$VTOC_not_mounted: defined in vtoc/vtoc.h -- fm/ raises it too
 * (bead source-3uo). */
#define status_$VTOC_not_found      0x20005     /* VTOCE not found in chain */
#define status_$VTOC_invalid_vtoce  0x20006     /* Invalid VTOCE */
#define status_$VTOC_uid_mismatch   0x80020002  /* UID mismatch; also returned by
                                                   VTOC_$ALLOCATE (0xE38BF6) when the
                                                   freshly allocated VTOCE block has no
                                                   free entry */
/* status_$vtoc_duplicate_uid (0x20007) is defined in vtoc/vtoc.h --
 * FILE_$PRIV_CREATE tests for it, so it is public. */
#define status_$no_UID              0x20004     /* No UID found */
#define status_$end_of_file         0x20003     /* End of file */

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
 * VTOC bucket slot (new format), 12 bytes
 *
 * Verified against VTOC_$ALLOCATE (0x00E388AC): the slot scan starts at
 * bucket+0x08 with a 12-byte stride, tests the block_info long at
 * bucket+0x10+i*12 (0xE389B0 tst.l (0x10,A0)) and compares the UID at
 * bucket+0x08+i*12 with two cmpm.l (0xE389C2/0xE389C6).
 */
typedef struct vtoc_$bucket_slot_t {
    uid_t       uid;                /* 0x00: UID of the object */
    uint32_t    block_info;         /* 0x08: VTOCE location (block << 4 | entry) */
} vtoc_$bucket_slot_t;

/*
 * VTOC bucket entry (new format), 0xF8 bytes
 *
 * Used for hash-based lookup on new format volumes.
 * Bucket n of a block lives at block + n*0xF8 (the compiler emits the
 * multiply as (n<<8) - (n<<3); see 0xE38996-0xE389A0).
 */
typedef struct vtoc_$bucket_entry_t {
    uint32_t                next_bucket;    /* 0x00: Next bucket block in chain (0 = end) */
    uint16_t                next_bkt_idx;   /* 0x04: Bucket index within next_bucket */
    uint16_t                reserved_06;    /* 0x06 */
    vtoc_$bucket_slot_t     slots[VTOCE_BUCKET_SLOTS];  /* 0x08: 20 UID slots */
} vtoc_$bucket_entry_t;

#if defined(ARCH_M68K)
_Static_assert(sizeof(vtoc_$bucket_slot_t) == 12,
               "vtoc_$bucket_slot_t must be 12 bytes");
_Static_assert(__builtin_offsetof(vtoc_$bucket_slot_t, block_info) == 0x08,
               "vtoc_$bucket_slot_t.block_info must be at 0x08");
_Static_assert(sizeof(vtoc_$bucket_entry_t) == VTOC_BUCKET_ENTRY_SIZE,
               "vtoc_$bucket_entry_t must be 0xF8 bytes");
_Static_assert(__builtin_offsetof(vtoc_$bucket_entry_t, next_bkt_idx) == 0x04,
               "vtoc_$bucket_entry_t.next_bkt_idx must be at 0x04");
_Static_assert(__builtin_offsetof(vtoc_$bucket_entry_t, slots) == 0x08,
               "vtoc_$bucket_entry_t.slots must be at 0x08");
#endif

/*
 * VTOC bucket block (new format), one 1024-byte disk block
 *
 * VTOC_$ALLOCATE initialises a freshly allocated bucket block by zeroing
 * 254 longwords (0xE38B70-0xE38B78) and then storing a magic number and the
 * block's own number in the last two longwords (0xE38B7E/0xE38B86).
 */
#define VTOC_BKT_BLOCK_MAGIC    0xFEDCA985u     /* 0xE38B7E: move.l #-0x123567b */
#define VTOC_BKTS_PER_BLOCK     4               /* wrap at 4: 0xE38AEE cmpi.w #0x4 */

typedef struct vtoc_$bkt_block_t {
    vtoc_$bucket_entry_t    buckets[VTOC_BKTS_PER_BLOCK];   /* 0x000: 4 * 0xF8 */
    uint8_t                 reserved_3e0[0x18];             /* 0x3E0 */
    uint32_t                magic;                          /* 0x3F8 */
    uint32_t                self_block;                     /* 0x3FC */
} vtoc_$bkt_block_t;

#if defined(ARCH_M68K)
_Static_assert(sizeof(vtoc_$bkt_block_t) == 0x400,
               "vtoc_$bkt_block_t must be one 1024-byte block");
_Static_assert(__builtin_offsetof(vtoc_$bkt_block_t, magic) == 0x3F8,
               "vtoc_$bkt_block_t.magic must be at 0x3F8");
_Static_assert(__builtin_offsetof(vtoc_$bkt_block_t, self_block) == 0x3FC,
               "vtoc_$bkt_block_t.self_block must be at 0x3FC");
#endif

/*
 * On-disk VTOCE header, common to both formats
 *
 * Verified against VTOC_$ALLOCATE (0x00E388AC):
 *   +0x00  byte, set to 1 for a newly allocated entry (0xE38C1E)
 *   +0x02  word, bit 15 = "entry in use".  Set on the caller's VTOCE with
 *          bset.b #0x7,(0x2,A1) at 0xE388CE and tested with tst.w/bmi at
 *          0xE38BD4 (new format, offset 0xA from the block) and 0xE38D22
 *          (old format, offset 0x6 from the block).
 *   +0x04  the object UID; compared with two cmpm.l at 0xE389C2 / 0xE38D38
 *          against the caller's VTOCE+4.
 */
typedef struct vtoce_$hdr_t {
    uint8_t     type_mode;          /* 0x00 */
    uint8_t     flags;              /* 0x01 */
    int16_t     status;             /* 0x02: bit 15 (0x8000) = entry in use */
    uid_t       uid;                /* 0x04: object UID */
} vtoce_$hdr_t;

#define VTOCE_STATUS_IN_USE     ((int16_t)0x8000)

typedef struct vtoce_$old_disk_t {
    vtoce_$hdr_t    hdr;                            /* 0x00 */
    uint8_t         rest[VTOCE_OLD_SIZE - 0x0C];    /* 0x0C */
} vtoce_$old_disk_t;

typedef struct vtoce_$new_disk_t {
    vtoce_$hdr_t    hdr;                            /* 0x00 */
    uint8_t         rest[VTOCE_NEW_SIZE - 0x0C];    /* 0x0C */
} vtoce_$new_disk_t;

/*
 * Old-format VTOC block: a 4-byte chain header followed by 5 VTOCEs.
 * The chain long is read at 0xE38D64 and written at 0xE38D94; VTOCE i is
 * addressed as block + 4 + i*0xCC (see the pea (0x4,A2) at 0xE38E5A).
 */
typedef struct vtoc_$old_block_t {
    uint32_t            next_block;                             /* 0x000 */
    vtoce_$old_disk_t   entries[VTOCE_OLD_ENTRIES_PER_BLOCK];   /* 0x004 */
} vtoc_$old_block_t;

/*
 * New-format VTOCE block: an 8-byte header followed by 3 VTOCEs.
 * The entry_count word at +4 is maintained by BAT_$ALLOC_VTOCE
 * (0xE3B09C addq.w #0x1,(0x4,A0) / 0xE3B0A0 cmpi.w #0x3).
 */
#define VTOCE_NEW_ENTRIES_PER_BLOCK     3

typedef struct vtoc_$vtoce_block_t {
    uint32_t            next_block;                             /* 0x000 */
    uint16_t            entry_count;                            /* 0x004 */
    uint16_t            reserved_06;                            /* 0x006 */
    vtoce_$new_disk_t   entries[VTOCE_NEW_ENTRIES_PER_BLOCK];   /* 0x008 */
} vtoc_$vtoce_block_t;

#if defined(ARCH_M68K)
_Static_assert(__builtin_offsetof(vtoce_$hdr_t, status) == 0x02,
               "vtoce_$hdr_t.status must be at 0x02");
_Static_assert(__builtin_offsetof(vtoce_$hdr_t, uid) == 0x04,
               "vtoce_$hdr_t.uid must be at 0x04");
_Static_assert(sizeof(vtoce_$old_disk_t) == VTOCE_OLD_SIZE,
               "vtoce_$old_disk_t must be 0xCC bytes");
_Static_assert(sizeof(vtoce_$new_disk_t) == VTOCE_NEW_SIZE,
               "vtoce_$new_disk_t must be 0x150 bytes");
_Static_assert(__builtin_offsetof(vtoc_$old_block_t, entries) == 0x04,
               "vtoc_$old_block_t.entries must be at 0x04");
_Static_assert(sizeof(vtoc_$old_block_t) == 0x400,
               "vtoc_$old_block_t must be one 1024-byte block");
_Static_assert(__builtin_offsetof(vtoc_$vtoce_block_t, entry_count) == 0x04,
               "vtoc_$vtoce_block_t.entry_count must be at 0x04");
_Static_assert(__builtin_offsetof(vtoc_$vtoce_block_t, entries) == 0x08,
               "vtoc_$vtoce_block_t.entries must be at 0x08");
_Static_assert(sizeof(vtoc_$vtoce_block_t) == 0x3F8,
               "vtoc_$vtoce_block_t must be 0x3F8 bytes");
#endif

/*
 * Per-volume VTOC record (100 bytes)
 *
 * The kernel keeps A5 = OS_DISK_DATA and addresses this record with signed
 * displacements from OS_DISK_DATA + vol_idx*100 (0xE3893A:
 * moveq #0x64,D5 / mulu.w D4w,D5 / lea (0x0,A5,D5w),A3).  The lowest
 * displacement in use is -0x54, so this struct starts there and
 * VTOC_VOL(vol_idx) == OS_DISK_DATA + vol_idx*100 - 0x54.  Volume indices
 * are 1-based, so volume 1 occupies bytes 0x10..0x73 of vtoc_$data.
 */
/*
 * One entry of the per-volume VTOC partition table: `count` VTOC blocks
 * (old format) or bucket blocks (new format) starting at disk block `base`.
 * VTOC_$GET_UID walks ten of them on a new-format volume (`moveq #0x9,D5`
 * at 0x00E39222) and eight on an old-format one (`moveq #0x7,D5` at
 * 0x00E39238); the table itself has room for ten (0x18 .. 0x54).
 */
typedef struct __attribute__((packed)) vtoc_$vol_part_t {
    uint16_t    count;              /* +0x00: blocks in this partition */
    uint32_t    base;               /* +0x02: first disk block */
} vtoc_$vol_part_t;

#define VTOC_VOL_PARTS  10

_Static_assert(sizeof(vtoc_$vol_part_t) == 6, "vtoc_$vol_part_t is 6 bytes");
_Static_assert(__builtin_offsetof(vtoc_$vol_part_t, base) == 2, "vtoc_$vol_part_t.base");

typedef struct vtoc_$vol_t {
    uint16_t    hash_type;          /* 0x00 (-0x54): 0=UID_$HASH, 2=shift-XOR, 3=XOR */
    uint16_t    hash_size;          /* 0x02 (-0x52): hash table size divisor */
    uint32_t    blocks_added;       /* 0x04 (-0x50): VTOC/bucket blocks allocated
                                     *   (0xE38ADC and 0xE38DCC addq.l #0x1) */
    uint32_t    name_dir1;          /* 0x08 (-0x4C) */
    uint32_t    name_dir2;          /* 0x0C (-0x48) */
    uint32_t    current_vtoce;      /* 0x10 (-0x44) */
    uint8_t     reserved_14[4];     /* 0x14 (-0x40) */
    vtoc_$vol_part_t parts[VTOC_VOL_PARTS]; /* 0x18 (-0x3C): the VTOC partition
                                     *   table, ten 6-byte {count, base} pairs
                                     *   (VTOC_$GET_UID 0x00E39282 cmp.w (-0x3c,A0)
                                     *   / 0x00E39298 add.l (-0x3a,A1,D7) with the
                                     *   cursor advancing `addq.l #0x6,A0`) */
    uint32_t    cur_bkt_block;      /* 0x54 (+0x00): bucket block being filled
                                     *   (0xE38A9E tst.l (A3)) */
    uint16_t    cur_bkt_idx;        /* 0x58 (+0x04): next bucket in that block,
                                     *   wraps at 4 (0xE38AE0-0xE38AF6) */
    uint8_t     reserved_5a[10];    /* 0x5A (+0x06) */
} vtoc_$vol_t;

#if defined(ARCH_M68K)
_Static_assert(sizeof(vtoc_$vol_t) == 100,
               "vtoc_$vol_t must be 100 bytes (the per-volume stride)");
_Static_assert(__builtin_offsetof(vtoc_$vol_t, blocks_added) == 0x04,
               "vtoc_$vol_t.blocks_added must be at -0x50");
_Static_assert(__builtin_offsetof(vtoc_$vol_t, current_vtoce) == 0x10,
               "vtoc_$vol_t.current_vtoce must be at -0x44");
_Static_assert(__builtin_offsetof(vtoc_$vol_t, parts) == 0x18,
               "vtoc_$vol_t.parts must be at -0x3C");
_Static_assert(__builtin_offsetof(vtoc_$vol_t, parts[9].base) == 0x50,
               "vtoc_$vol_t.parts[9].base must be at -0x04");
_Static_assert(__builtin_offsetof(vtoc_$vol_t, cur_bkt_block) == 0x54,
               "vtoc_$vol_t.cur_bkt_block must be at +0x00");
_Static_assert(__builtin_offsetof(vtoc_$vol_t, cur_bkt_idx) == 0x58,
               "vtoc_$vol_t.cur_bkt_idx must be at +0x04");
#endif

/* vtoc_$data_t and vtoc_$data: declared in vtoc/vtoc.h -- fm/ reads the
 * mount, format and write-protect arrays (bead source-3uo). */

/*
 * Disk data base address - the same object as vtoc_$data, viewed as a byte
 * array for the per-volume offset arithmetic (vol_idx * 100 - 0x54 etc.).
 */
#define OS_DISK_DATA    ((uint8_t *)&vtoc_$data)    /* 0xE784D0 */

/*
 * UID constants for VTOC block types are exported from vtoc/vtoc.h
 * (VTOC_$UID, VTOC_BKT_$UID) -- bat/ and others need them.
 */

/*
 * Special UIDs for ACL defaults
 * (PPO_$NIL_USER_UID / PPO_$NIL_ORG_UID are declared in vtoc/vtoc.h)
 */

/*
 * UID cache structure for quick VTOCE lookup
 *
 * Located at 0xEB2C00
 * 101 buckets, 4 entries per bucket (0x40 bytes per bucket)
 */
#define VTOC_UID_CACHE_BUCKETS  101
#define VTOC_UID_CACHE_ENTRIES  4

/*
 * Layout recovered from vtoc_$uid_cache_insert (0x00E382A8) and
 * vtoc_$uid_cache_lookup (0x00E38324):
 *   +0x00  uid         compared with two cmpm.l (0x00E382E2 / 0x00E38362)
 *   +0x08  block_info  `move.l (0xe,A6),(0x8,A2)` at 0x00E3830C, read back
 *                      by the lookup at 0x00E3838A
 *   +0x0C  vol         the WORD the insert stores its vol_idx argument in
 *                      (0x00E38312); non-zero means the entry is valid
 *                      (`tst.w (0xc,A0)` at 0x00E382EA / 0x00E3836A) and it
 *                      is what the lookup returns through its flags pointer
 *                      (0x00E38384)
 *   +0x0E  age         cleared by the insert (0x00E38316), aged by the lookup
 *                      (`cmpi.w #-0x2,(0xe,A0)` / `addq.w #0x1` at
 *                      0x00E38390), largest wins replacement (0x00E382F0)
 * The bucket index is the word at uid+2 (`move.w (0x2,A1),D0w`), i.e. the
 * low half of uid.high, modulo 101.
 */
typedef struct vtoc_$uid_cache_entry_t {
    uid_t       uid;                /* 0x00: UID */
    uint32_t    block_info;         /* 0x08: Block info (block << 4 | entry) */
    uint16_t    vol;                /* 0x0C: volume index; 0 = entry invalid */
    uint16_t    age;                /* 0x0E: age counter (saturates at 0xFFFE) */
} vtoc_$uid_cache_entry_t;

typedef struct vtoc_$uid_cache_bucket_t {
    vtoc_$uid_cache_entry_t entries[VTOC_UID_CACHE_ENTRIES];  /* 16 bytes each */
} vtoc_$uid_cache_bucket_t;

_Static_assert(sizeof(vtoc_$uid_cache_entry_t) == 0x10, "vtoc_$uid_cache_entry_t is 16 bytes");
_Static_assert(__builtin_offsetof(vtoc_$uid_cache_entry_t, vol) == 0x0C, "vtoc_$uid_cache_entry_t.vol");
_Static_assert(__builtin_offsetof(vtoc_$uid_cache_entry_t, age) == 0x0E, "vtoc_$uid_cache_entry_t.age");
_Static_assert(sizeof(vtoc_$uid_cache_bucket_t) == 0x40, "vtoc_$uid_cache_bucket_t is 0x40 bytes (lsl.l #0x6)");

extern vtoc_$uid_cache_bucket_t vtoc_$uid_cache[VTOC_UID_CACHE_BUCKETS];

/*
 * Helper macros
 */

/* VTOC_IS_MOUNTED / VTOC_IS_NEW_FORMAT: defined in vtoc/vtoc.h -- FM_$READ
 * and FM_$WRITE test both (bead source-3uo). */

/* Get per-volume data pointer (see vtoc_$vol_t: the record starts 0x54
 * bytes below the address the kernel computes as OS_DISK_DATA + n*100) */
#define VTOC_VOL(vol_idx) \
    ((vtoc_$vol_t *)(OS_DISK_DATA + (vol_idx) * 100 - 0x54))

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

/* Hash UID to bucket for lookup (vtoc_$hash_uid, 0x00e383b0) */
void vtoc_$hash_uid(uid_t *uid, short vol_idx, uint16_t *bucket_idx,
                    uint32_t *block, status_$t *status);

/* UID cache lookup (vtoc_$uid_cache_lookup, 0x00e38324).  Returns a Domain
 * boolean in D0b; `remove` is the byte at A6+0x14: negative empties the
 * matching entry (`move.l #0xffff,(0xc,A0)` = vol 0, age 0xFFFF). */
uint8_t vtoc_$uid_cache_lookup(uid_t *uid, uint16_t *flags, uint32_t *block_info, char remove);

/* 0x00E38F7E: the NEW_TO_OLD flags byte shared by VTOC_$ALLOCATE and
 * VTOCE_$WRITE (vtoc_data.c) */
extern char vtoc_$new_to_old_flags_00e38f7e;

/* UID cache insert (uid_cache.c) */
void vtoc_$uid_cache_insert(uid_t *uid, int16_t vol_idx, uint32_t block_info);

/* File map block allocation/traversal (vtoc_$fm_traverse, 0x00e397d0) */
uint16_t vtoc_$fm_traverse(uint32_t *block_ptr, uint16_t level, uint32_t hint);

/* Indirect block freeing helper (vtoc_$free_indirect, 0x00e39bc2) */
void vtoc_$free_indirect(uint32_t block, uint16_t level, uint32_t limit,
                         uint32_t step, char do_free);

/*
 * ============================================================================
 * Audit event data records
 * ============================================================================
 *
 * VTOC_$MOUNT and VTOC_$DISMOUNT each hand AUDIT_$LOG_EVENT one flat stack
 * record.  The original code indexes them as Pascal 1-based byte arrays
 * (`(-0x39,A6,D1w)` with D1 = 1..0x20 at 0x00E38664), so element N sits at
 * C offset N-1.
 */

/* 0x32 bytes at A6-0x38 in VTOC_$MOUNT (length cell 0x00E38762 = 0x32) */
typedef struct vtoc_$audit_mount_rec_t {
    char        vol_name[32];   /* 0x00: label_block+0x04 .. +0x23 */
    uint8_t     reserved_20[4]; /* 0x20: cleared at 0x00E38674 */
    uid_t       vol_uid;        /* 0x24: label_block+0x24 (0x00E38658) */
    uint16_t    param_2;        /* 0x2C: the caller's param_2 (0x00E3870A) */
    uint16_t    vol_idx;        /* 0x2E: 0x00E38706 */
    uint8_t     wp_flag;        /* 0x30: vtoc unit byte +0x26F (0x00E38700) */
    uint8_t     pad_31;         /* 0x31 */
} vtoc_$audit_mount_rec_t;

/* 0x30 bytes at A6-0x30 in VTOC_$DISMOUNT (length cell 0x00E388AA = 0x30) */
typedef struct vtoc_$audit_dismount_rec_t {
    char        vol_name[32];   /* 0x00: label_block+0x04 .. +0x23 */
    uint8_t     reserved_20[4]; /* 0x20: cleared at 0x00E387FC */
    uid_t       vol_uid;        /* 0x24: label_block+0x24 (0x00E387E0) */
    uint16_t    vol_idx;        /* 0x2C: 0x00E38882 */
    uint8_t     flags;          /* 0x2E: 0x00E3887E */
    uint8_t     pad_2f;         /* 0x2F */
} vtoc_$audit_dismount_rec_t;

#if defined(ARCH_M68K)
_Static_assert(offsetof(vtoc_$audit_mount_rec_t, vol_uid) == 0x24, "amount.vol_uid");
_Static_assert(sizeof(vtoc_$audit_mount_rec_t) == 0x32, "sizeof amount rec");
_Static_assert(offsetof(vtoc_$audit_dismount_rec_t, vol_uid) == 0x24, "admount.vol_uid");
_Static_assert(sizeof(vtoc_$audit_dismount_rec_t) == 0x30, "sizeof admount rec");
#endif

#endif /* VTOC_INTERNAL_H */
