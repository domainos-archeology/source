/*
 * AREA - Area (Multi-Segment) Management
 *
 * This module provides area-level operations for Domain/OS virtual memory.
 * An "area" is a contiguous virtual address range that can span multiple
 * segments. Areas support:
 *   - Dynamic growth and shrinkage
 *   - Copy-on-write duplication
 *   - Remote (networked) backing storage
 *   - Association with AST (Address Space Table) entries
 *
 * Memory layout (m68k):
 *   - Area table base: 0xD94C00 (each entry 0x30 bytes)
 *   - Module globals: 0xE1E118
 *   - Maximum areas: 0x3A (58)
 *
 * Area IDs are calculated as: ((entry_ptr - 0xD94C00) / 0x30) + 1
 * An area handle combines generation (high word) and area ID (low word).
 *
 * Lock: ML_LOCK_AREA (0x0E) for area table operations
 *       ML_LOCK_AST (0x12) for AST operations within area functions
 *          (AREA_$COPY 0x00E09298/0x00E092FE, area_$alloc_seg_table
 *           0x00E09D46/0x00E09E28 - all push #0x12)
 *       ML_LOCK_PMAP (0x14) for the page-map operations AREA_$TOUCH
 *          (0x00E0964A/0x00E0968E), AREA_$ASSOC (0x00E096E4) and
 *          AREA_$TRANSFER (0x00E08176) take
 */

#ifndef AREA_H
#define AREA_H

#include "base/base.h"
#include "ec/ec.h"

/*
 * Lock ID for AREA operations
 */
#define ML_LOCK_AREA    0x0E

/*
 * Area table constants
 */
#define AREA_TABLE_BASE         0xD94C00
#define AREA_ENTRY_SIZE         0x30        /* 48 bytes per entry */
#define AREA_MAX_ENTRIES        0x3A        /* 58 entries */

/*
 * Area module globals base address (the A5 every AREA_ routine loads)
 */
#define AREA_GLOBALS_BASE       0xE1E118

/*
 * Number of RPMAP-cache pages a diskless node wires in AREA_$INIT.
 * 0x00E2F448 `moveq #0x2,D2` + `dbf` = 3 iterations, mapping 0xEE4C00,
 * 0xEE5000 and 0xEE5400 (AREA_$RPMAP_CACHE in the SAU2 map is at 0xEE4C00).
 */
#define AREA_DISKLESS_PAGE_COUNT    3

/* First virtual address of the RPMAP cache window (0x00E2F44E `movea.l
 * #0xee4c00,A3` then `lea (0x400,A3),A3`, and the loop subtracts 0x400 back
 * off before each MMU_$INSTALL): the map's AREA_$RPMAP_CACHE (EE4C00, the
 * first of VM_TABLES' run-time windows, AST_PMAPS_END).  Virtual only;
 * sau2.ld places it at OS_PAGE_END (layout_vm.ld, docs/rfc-cold-start.md
 * section 8c, source-o7s2): the literal lay inside our MMAP_$MMAPE. */
extern char AREA_$RPMAP_CACHE[];
#define AREA_RPMAP_CACHE_VA         ARCH_PTR_TO_VA(AREA_$RPMAP_CACHE)

/* Page size the loop steps by (0x00E2F4BA `addi.l #0x400,D4`). */
#define AREA_RPMAP_PAGE_SIZE        0x400

/* The overflow-slot page window of the seg-table pool: record i's page is
 * mapped at 0xEE6400 + i * 0x400 (area_$alloc_seg_table 0x00E09D6E
 * `movea.l #0xee6400,A0`, area_$free_seg_table 0x00E09E6E).  SAU2 map:
 * `EE6400  PIT_PAGES`, a run-time window of VM_TABLES that sau2.ld places
 * past OS_PAGE_END at the map's offset from AREA_$RPMAP_CACHE
 * (layout_vm.ld, docs section 8c, source-o7s2). */
extern char PIT_PAGES[];
#define AREA_PIT_PAGES_VA           ARCH_PTR_TO_VA(PIT_PAGES)

/*
 * Area entry flags (in flags field at offset 0x2E)
 */
#define AREA_FLAG_ACTIVE        0x0001      /* Area is active/in-use */
#define AREA_FLAG_REVERSED      0x0002      /* Segment ordering is reversed */
#define AREA_FLAG_TOUCHED       0x0004      /* Area has been touched */
#define AREA_FLAG_SHARED        0x0008      /* Area is shared */
#define AREA_FLAG_IN_TRANS      0x0010      /* Area operation in progress */

/*
 * Status codes for AREA operations (module 0x32)
 */
#define status_$area_none_free          0x00320001  /* No free area entries */
#define status_$area_bad_handle         0x00320002  /* Invalid area handle */
#define status_$area_bad_offset         0x00320003  /* Invalid offset */
#define status_$area_create_failed      0x00320004  /* Create operation failed */
#define status_$area_not_active         0x00320006  /* Area not active */
#define status_$area_not_owner          0x00320007  /* Caller doesn't own area */
#define status_$area_not_found          0x00320008  /* Area not found */
#define status_$area_no_free_resources  0x00320005  /* No free resources */
#define status_$area_bad_reserve        0x0032000B  /* Invalid reserve size */

/*
 * Area entry structure
 * Size: 0x30 bytes (48 bytes)
 *
 * Areas are organized in per-ASID linked lists (via next/prev).
 * The seg_bitmap tracks which segments (0-63) have been allocated.
 * For areas with more than 16 segments, extended bitmap tables are used.
 */
typedef struct area_$entry_t {
    struct area_$entry_t *next;     /* 0x00: Next in per-ASID list */
    struct area_$entry_t *prev;     /* 0x04: Previous in per-ASID list */
    uint32_t virt_size;             /* 0x08: Virtual size (bytes, 32KB aligned) */
    uint32_t commit_size;           /* 0x0C: Committed/reserved size (bytes) */
    uint32_t caller_id;             /* 0x10: Caller-provided unique ID (for dedup) */
    int16_t first_bste;             /* 0x14: First BSTE index, -1 if unset */
    int16_t first_seg_index;        /* 0x16: First segment index in area */
    uint32_t seg_bitmap[2];         /* 0x18-0x1F: Segment allocation bitmap */
    uint32_t remote_uid;            /* 0x20: Remote UID for networked areas */
    int16_t volx;                   /* 0x24: Local volume index */
    int16_t owner_asid;             /* 0x26: Owner address space ID */
    int16_t remote_volx;            /* 0x28: Remote volume index */
    int16_t area_id;                /* 0x2A: the entry's own 1-based id,
                                     * written when area_$alloc_resources
                                     * threads it onto the free list
                                     * (`move.w D2w,(-0x6,A0)` 0x00E076F4,
                                     * 0x00E07722); AREA_$FREE_ASID and
                                     * AREA_$COPY hand it to
                                     * area_$internal_delete */
    int16_t generation;             /* 0x2C: Generation number (for handle validation) */
    uint16_t flags;                 /* 0x2E-0x2F: Flags (see AREA_FLAG_*) */
} area_$entry_t;

#if defined(ARCH_M68K)
/* Offsets recovered from area_$internal_create (0x00E077DA) and
 * area_$alloc_resources (0x00E075CA); entry stride is the divu.w #0x30 at
 * 0x00E078D4. */
_Static_assert(sizeof(area_$entry_t) == AREA_ENTRY_SIZE, "area_$entry_t size");
_Static_assert(offsetof(area_$entry_t, next)        == 0x00, "next");
_Static_assert(offsetof(area_$entry_t, prev)        == 0x04, "prev");
_Static_assert(offsetof(area_$entry_t, virt_size)   == 0x08, "virt_size");
_Static_assert(offsetof(area_$entry_t, commit_size) == 0x0C, "commit_size");
_Static_assert(offsetof(area_$entry_t, caller_id)   == 0x10, "caller_id");
_Static_assert(offsetof(area_$entry_t, first_bste)  == 0x14, "first_bste");
_Static_assert(offsetof(area_$entry_t, first_seg_index) == 0x16, "first_seg_index");
_Static_assert(offsetof(area_$entry_t, seg_bitmap)  == 0x18, "seg_bitmap");
_Static_assert(offsetof(area_$entry_t, remote_uid)  == 0x20, "remote_uid");
_Static_assert(offsetof(area_$entry_t, volx)        == 0x24, "volx");
_Static_assert(offsetof(area_$entry_t, owner_asid)  == 0x26, "owner_asid");
_Static_assert(offsetof(area_$entry_t, remote_volx) == 0x28, "remote_volx");
_Static_assert(offsetof(area_$entry_t, area_id)     == 0x2A, "area_id");
_Static_assert(offsetof(area_$entry_t, generation)  == 0x2C, "generation");
_Static_assert(offsetof(area_$entry_t, flags)       == 0x2E, "flags");
#endif

/*
 * Area handle type
 * High word: generation number
 * Low word: area ID (1-based index into area table)
 */
typedef uint32_t area_$handle_t;

/*
 * Extract area ID from handle
 */
#define AREA_HANDLE_TO_ID(h)    ((h) & 0xFFFF)

/*
 * Extract generation from handle
 */
#define AREA_HANDLE_TO_GEN(h)   ((h) >> 16)

/*
 * Build handle from generation and ID
 */
#define AREA_MAKE_HANDLE(gen, id)   (((uint32_t)(gen) << 16) | ((id) & 0xFFFF))

/*
 * Convert area ID to entry pointer
 */
#define AREA_ID_TO_ENTRY(id)    ((area_$entry_t *)(AREA_TABLE_BASE + ((id) - 1) * AREA_ENTRY_SIZE))

/*
 * Convert entry pointer to area ID
 */
#define AREA_ENTRY_TO_ID(ptr)   ((int16_t)(((uintptr_t)(ptr) - AREA_TABLE_BASE) / AREA_ENTRY_SIZE + 1))

/*
 * UID hash table entry for area deduplication
 * Used by AREA_$CREATE_FROM to find existing areas for the same remote UID
 */
typedef struct area_$uid_hash_t {
    struct area_$uid_hash_t *next;  /* 0x00: Next in hash chain */
    area_$entry_t *first_entry;     /* 0x04: First area entry with this UID */
} area_$uid_hash_t;

#if defined(ARCH_M68K)
/* AREA_$INIT (0x00E2F3FC-0x00E2F40A) walks the pool with `addq.l #0x8` and the
 * chain field is (A2) with the entry list at (0x4,A2) (0x00E07AEE-0x00E07B32). */
_Static_assert(sizeof(area_$uid_hash_t) == 8, "area_$uid_hash_t size");
_Static_assert(offsetof(area_$uid_hash_t, next)        == 0x00, "hash next");
_Static_assert(offsetof(area_$uid_hash_t, first_entry) == 0x04, "hash first_entry");
#endif

/*
 * Number of UID hash buckets.
 * AREA_$CREATE_FROM hashes with M$OIU$WLW(uid, 11) at 0x00E07A2C, and
 * AREA_$INIT clears 11 buckets (moveq #0xa + dbf at 0x00E2F3EC).
 */
#define AREA_UID_HASH_BUCKETS   11

/*
 * ============================================================================
 * The AREA_ module data block
 * ============================================================================
 *
 * Every AREA_ routine begins `lea (0xe1e118).l,A5` and then reaches its data
 * with a displacement off A5, so all of it is ONE Pascal module block, not a
 * set of loose globals.  The SR10.2 SAU2 link map gives the extent and four
 * interior names:
 *
 *   D    E1E118  AREA_                      size = 5E8
 *        E1E150  AREA_$RPMAP_IN_TRANS_EC    (+0x038)
 *        E1E160  AREA_$IN_TRANS_EC          (+0x048)
 *        E1E170  AREA_$PITE_IN_TRANS_EC     (+0x058)
 *        E1E6E0  AREA_$FREE_LIST            (+0x5C8)
 *        E1E6E4  AREA_$PARTNER              (+0x5CC)
 *        E1E6EC  AREA_$FORMAT               (+0x5D4)
 *        E1E6F4  AREA_$DEL_DUP              (+0x5DC)
 *        E1E6F6  AREA_$CR_DUP               (+0x5DE)
 *        E1E6F8  AREA_$N_FREE               (+0x5E0)
 *        E1E6FA  AREA_$N_AREAS              (+0x5E2)
 *        E1E6FC  AREA_$PARTNER_PKT_SIZE     (+0x5E4)
 *
 * The remaining fields are recovered from AREA_$INIT (0x00E2F3A8) and the
 * RPMAP cache manager at 0x00E07370; the block tiles exactly, with no gaps
 * between +0x000 and +0x5E6.  (source-vm49)
 */

/*
 * One entry of the three-slot RPMAP page cache.
 *
 * AREA_$INIT initialises three of these at globals+0x10 with a stride of 0x0C
 * (0x00E2F45A `lea (0xc,A4),A3` then `lea (0xc,A3),A3`), writing (A3+0x4),
 * (A3+0x8), (A3+0xA) = 0xFFFF, (A3+0xC) and (A3+0xD) - i.e. A3 is held
 * BIASED four bytes below the record, the usual Domain Pascal array-cursor
 * form.  The manager at 0x00E07370 holds the same biased cursor
 * (`lea (0xc,A5),A1`, fields at (0x4,A0) and (0xa,A0)) and writes slot i with
 * `move.l (0x5c0,A5),(0x4,A5,D3w*0x1)` where D3 = i * 0x0C and i is 1-based.
 */
typedef struct area_$rpmap_cache_t {
    uint32_t    seq;            /* 0x00: stamped from AREA_$GLOBALS.rpmap_seq
                                 *       on every hit; the LRU key */
    uint16_t    volx;           /* 0x04: the cached page's remote volume index,
                                 *       compared with area_$entry_t.remote_volx
                                 *       (area_$rpmap_get 0x00E073A4); cleared
                                 *       by AREA_$INIT */
    uint16_t    group;          /* 0x06: the cached segment group, seg_idx >> 3
                                 *       (0x00E0739A); AREA_$INIT sets 0xFFFF
                                 *       ("empty") */
    int8_t      dirty;          /* 0x08: Domain boolean - written back to the
                                 *       partner before the slot is reused
                                 *       (`st (0xc,A0)` 0x00E073CC) */
    int8_t      in_trans;       /* 0x09: Domain boolean - a read or write-back
                                 *       is in flight; waiters sleep on
                                 *       rpmap_in_trans_ec (0x00E073B2) */
    uint8_t     reserved_0a[2]; /* 0x0A: never read or written */
} area_$rpmap_cache_t;

/*
 * AREA_$FORMAT (map symbol at 0xE1E6EC = globals+0x5D4), four words.  The map
 * names no symbol between it and AREA_$DEL_DUP at 0xE1E6F4 (= globals+0x5DC),
 * so all four words belong to this record.
 *
 * AREA_$INIT writes +0x02 = 0x540 (0x00E2F3B6) and clears +0x04 and +0x06
 * (0x00E2F4CC/0x00E2F4D0).  area_$alloc_resources clamps its request against
 * +0x02 (globals+0x5D6), so that word is the maximum number of area_$entry_t
 * records the table may ever hold.
 *
 * The last two words are the seg-table pool's bookkeeping, recovered from
 * area_$alloc_seg_table (0x00E09D2E):
 *   +0x04 (globals+0x5D8)  the index of the next free pool record.  Read at
 *         0x00E09D6A to form the bitmap page VA (0xEE6400 + idx * 0x400) and
 *         again at 0x00E09DA8 to form the record address; rewritten by the
 *         free-slot scan at 0x00E09DEE, or forced to 64 at 0x00E09E00 when
 *         the pool is full.
 *   +0x06 (globals+0x5DA)  the number of records handed out.  0x00E09D52
 *         `cmpi.w #0x40` refuses the allocation (returning NIL) when it has
 *         reached 64, and 0x00E09DA4 `addq.w #0x1` bumps it.
 */
typedef struct area_$format_t {
    uint16_t    word_00;        /* 0x00 (+0x5D4): never touched by AREA_$INIT */
    uint16_t    max_entries;    /* 0x02 (+0x5D6): 0x540 = 1344 */
    uint16_t    seg_table_next; /* 0x04 (+0x5D8): next free seg_table_pool[] */
    uint16_t    seg_table_count;/* 0x06 (+0x5DA): seg_table_pool[] records in use */
} area_$format_t;

/*
 * area_$seg_slot_t - one four-byte cell of an area's segment map.
 *
 * area_$entry_t.seg_bitmap[] is two of these (entry+0x18 and entry+0x1C) and
 * the overflow tables an area_$seg_table_t owns are arrays of them; both
 * AREA_$COPY cursors step by four bytes (0x00E0936A/0x00E0936E
 * `addq.l #0x4`) and the overflow offset is scaled by four as well
 * (0x00E0921C `lsl.w #0x2,D0w`).
 *
 * The field offsets are area_$get_aste's (0x00E09A6A), which is handed one
 * of these cells as its second argument:
 *   +0x00  the eight "segment allocated" bits - 0x00E09B88 `move.b (A4),D0b`
 *          followed by `btst.l D3,D0` with D3 = seg_index & 7, and the same
 *          byte is what AREA_$COPY tests at 0x00E09284.
 *   +0x01  a state byte - 0x00E09A96 `lea (0x1,A1),A2` then 0x00E09AA0
 *          `btst.b #0x6,(A2)` (the "in transition" wait), and 0x00E09B06
 *          `bset.b #0x6,(0x1,A0)`.
 *   +0x02  the 1-based ASTE index - 0x00E09AAE `move.w (0x2,A0),D0w`, which
 *          0x00E09AB2-0x00E09AC0 scales by 0x14 into the ASTE table at
 *          0xEC5400.
 *
 * Naming the byte gives the same address on either endianness; casting the
 * longword to `uint8_t *` would not.
 */
typedef struct area_$seg_slot_t {
    uint8_t  bits;              /* 0x00: eight "segment allocated" flags */
    uint8_t  state;             /* 0x01: bit 6 = ASTE in transition */
    uint16_t aste_index;        /* 0x02: 1-based index, AST_ASTE_ENTRY */
} area_$seg_slot_t;

#if defined(ARCH_M68K)
_Static_assert(sizeof(area_$seg_slot_t) == 4, "area_$seg_slot_t size");
_Static_assert(offsetof(area_$seg_slot_t, bits)       == 0x00, "seg_slot.bits");
_Static_assert(offsetof(area_$seg_slot_t, state)      == 0x01, "seg_slot.state");
_Static_assert(offsetof(area_$seg_slot_t, aste_index) == 0x02, "seg_slot.aste_index");
#endif

/*
 * Extended segment table entry for areas with > 16 segments
 * Used by AREA_$COPY and segment threading operations.
 *
 * All five fields are written by area_$alloc_seg_table (0x00E09DBE .. 
 * 0x00E09E18); `allocated` is the pool's in-use flag, set with `st` and read
 * back by the free-slot scan's `tst.b` / `bmi`, so it is a Domain boolean.
 *
 * `next` and `bitmap_ptr` are the record's two 32-bit target virtual
 * addresses; they are spelled uint32_t (and dereferenced through
 * ARCH_VA_TO_PTR) rather than as host pointers so the record stays twelve
 * bytes on a 64-bit host and the layout asserts below can be unconditional.
 * (source-efc9)
 */
typedef struct area_$seg_table_t {
    int16_t  area_id;           /* 0x00: Area ID */
    uint8_t  table_index;       /* 0x02: Table index (0-255) */
    int8_t   allocated;         /* 0x03: pool in-use flag (0x00E09DBE `st`) */
    uint32_t next;              /* 0x04: VA of the next record in the ASID
                                 *       list (0x00E09E18) */
    uint32_t bitmap_ptr;        /* 0x08: VA of the overflow slots
                                 *       (0x00E09E06) */
} area_$seg_table_t;

_Static_assert(sizeof(area_$seg_table_t) == 0x0C,  /* AREA_SEG_TABLE_POOL_STRIDE */
               "area_$seg_table_t size");
_Static_assert(offsetof(area_$seg_table_t, area_id)     == 0x00, "seg_table.area_id");
_Static_assert(offsetof(area_$seg_table_t, table_index) == 0x02, "seg_table.table_index");
_Static_assert(offsetof(area_$seg_table_t, allocated)   == 0x03, "seg_table.allocated");
_Static_assert(offsetof(area_$seg_table_t, next)        == 0x04, "seg_table.next");
_Static_assert(offsetof(area_$seg_table_t, bitmap_ptr)  == 0x08, "seg_table.bitmap_ptr");

/*
 * The seg-table pool: 64 area_$seg_table_t records at globals+0x150.
 *
 * AREA_$INIT clears one byte in each (0x00E2F4D4 `moveq #0x3f,D0` = 64
 * iterations, `clr.b (0x153,A0)` / `lea (0xc,A0),A0`), and the 64 * 0x0C =
 * 0x300 bytes tile globals+0x150 .. globals+0x44F exactly, between the
 * 58-longword seg-table list array that ends at +0x150 and uid_hash_free at
 * +0x450.
 *
 * area_$alloc_seg_table (0x00E09D2E) is what gives the region its type.  It
 * forms the record address as globals + index*0x0C + 0x150 - 0x00E09DA8
 * reads the cursor at globals+0x5D8, 0x00E09DAC-0x00E09DB4 multiplies it by
 * twelve (`lsl.l #0x2` then `add.l D1,D1` / `add.l D1,D0`), 0x00E09DB6
 * `lea (0x0,A5,D0*0x1),A1` and 0x00E09DBA `lea (0x150,A1),A0` - and then
 * fills exactly the area_$seg_table_t fields:
 *   0x00E09DBE  st (0x3,A0)            allocated = TRUE
 *   0x00E09E06  move.l D5,(0x8,A0)     bitmap_ptr = the fresh 0x400 page
 *   0x00E09E0A  move.w D2,(A0)         area_id
 *   0x00E09E0C  move.b D3,(0x2,A0)     table_index
 *   0x00E09E18  move.l (0x68,A1),(0x4,A0)   next = seg_table_list[asid]
 * The free-slot scan at 0x00E09DE8 `tst.b (0x153,A1)` (with A1 stepping by
 * 0x0C) reads back the same +0x03 byte as a Domain boolean, which is what
 * AREA_$INIT's clear initialises.  (source-tqkk)
 */
#define AREA_SEG_TABLE_POOL_COUNT   64
#define AREA_SEG_TABLE_POOL_STRIDE  0x0C

/* The whole AREA_ module data block. */
typedef struct area_$globals_t {
    /*
     * 0x000: the three physical pages AREA_$INIT wires for the RPMAP cache at
     * 0xEE4C00 (AREA_$RPMAP_CACHE in the map).  WP_$CALLOC fills cell i
     * (`pea (-0x4,A2)` with A2 = globals+4+i*4) and MMU_$INSTALL reads it back
     * (`move.l (-0x4,A2),-(SP)`), so the cells are +0x00, +0x04 and +0x08 -
     * NOT +0x08/+0x0C/+0x10.  (source-vm49)
     */
    uint32_t                rpmap_page[AREA_DISKLESS_PAGE_COUNT]; /* 0x000 */
    uint32_t                reserved_00c;       /* 0x00C: the biased cursor's
                                                 * landing pad; never read */
    area_$rpmap_cache_t     rpmap_cache[AREA_DISKLESS_PAGE_COUNT]; /* 0x010 */
    uint8_t                 reserved_034[4];    /* 0x034 */

    /* 0x038/0x048/0x058: the three map-named eventcounts, on a 0x10 stride
     * (ec_$eventcount_t is 0x0C bytes, so each carries 4 bytes of padding). */
    ec_$eventcount_t        rpmap_in_trans_ec;  /* 0x038 */
    uint8_t                 pad_044[4];         /* 0x044 */
    ec_$eventcount_t        in_trans_ec;        /* 0x048 */
    uint8_t                 pad_054[4];         /* 0x054 */
    ec_$eventcount_t        pite_in_trans_ec;   /* 0x058 */
    uint8_t                 pad_064[4];         /* 0x064 */

    /* 0x068: per-ASID extended-segment-table list heads.  AREA_$INIT clears
     * all 58 (0x00E2F3CE `moveq #0x39,D0`); AREA_$TRANSFER indexes it with
     * `lsl.w #0x2` on the ASID (0x00E08256-0x00E08258). */
    struct area_$seg_table_t *seg_table_list[AREA_MAX_ENTRIES];   /* 0x068 */

    /* 0x150: the pool area_$alloc_seg_table (0x00E09D2E) hands out, indexed
     * by the cursor in format.seg_table_next.  AREA_$INIT clears every
     * record's `allocated` byte; the allocator sets it with `st` and the
     * free-slot scan tests it. */
    area_$seg_table_t       seg_table_pool[AREA_SEG_TABLE_POOL_COUNT];  /* 0x150 */

    struct area_$uid_hash_t *uid_hash_free;                       /* 0x450 */
    struct area_$uid_hash_t *uid_hash[AREA_UID_HASH_BUCKETS];     /* 0x454 */
    struct area_$uid_hash_t  uid_hash_pool[AREA_UID_HASH_BUCKETS];/* 0x480 */
    struct area_$entry_t    *asid_list[AREA_MAX_ENTRIES];         /* 0x4D8 */

    /* 0x5C0: monotonic stamp the RPMAP cache manager increments and copies
     * into a slot's `seq` (0x00E073D0/0x00E073DE). */
    uint32_t                rpmap_seq;          /* 0x5C0 */
    uint32_t                next_caller_id;     /* 0x5C4 */
    struct area_$entry_t   *free_list;          /* 0x5C8 */
    uid_t                   partner;            /* 0x5CC */
    area_$format_t          format;             /* 0x5D4 */
    int16_t                 del_dup;            /* 0x5DC */
    int16_t                 cr_dup;             /* 0x5DE */
    int16_t                 n_free;             /* 0x5E0 */
    int16_t                 n_areas;            /* 0x5E2 */
    int16_t                 partner_pkt_size;   /* 0x5E4 */
    uint8_t                 reserved_5e6[2];    /* 0x5E6: block tail */
} area_$globals_t;

extern area_$globals_t AREA_$GLOBALS;

#if defined(ARCH_M68K)
/* Every offset used by AREA_$INIT (0x00E2F3A8) and the RPMAP manager
 * (0x00E07370), plus the four the SAU2 link map names.  The block size is the
 * map segment size: `D  E1E118  AREA_  size = 5E8`. */
_Static_assert(sizeof(area_$rpmap_cache_t) == 0x0C, "area_$rpmap_cache_t size");
_Static_assert(offsetof(area_$rpmap_cache_t, seq)     == 0x00, "rpmap_cache.seq");
_Static_assert(offsetof(area_$rpmap_cache_t, volx)     == 0x04, "rpmap_cache.volx");
_Static_assert(offsetof(area_$rpmap_cache_t, group)    == 0x06, "rpmap_cache.group");
_Static_assert(offsetof(area_$rpmap_cache_t, dirty)    == 0x08, "rpmap_cache.dirty");
_Static_assert(offsetof(area_$rpmap_cache_t, in_trans) == 0x09, "rpmap_cache.in_trans");
_Static_assert(sizeof(area_$format_t) == 0x08, "area_$format_t size");
_Static_assert(offsetof(area_$format_t, max_entries) == 0x02, "format.max_entries");
_Static_assert(offsetof(area_$format_t, seg_table_next)  == 0x04, "format.seg_table_next");
_Static_assert(offsetof(area_$format_t, seg_table_count) == 0x06, "format.seg_table_count");
_Static_assert(sizeof(((area_$globals_t *)0)->seg_table_pool) == 0x300,
               "globals.seg_table_pool spans +0x150..+0x44F");

_Static_assert(offsetof(area_$globals_t, rpmap_page)        == 0x000, "globals.rpmap_page");
_Static_assert(offsetof(area_$globals_t, rpmap_cache)       == 0x010, "globals.rpmap_cache");
_Static_assert(offsetof(area_$globals_t, rpmap_in_trans_ec) == 0x038, "globals.rpmap_in_trans_ec");
_Static_assert(offsetof(area_$globals_t, in_trans_ec)       == 0x048, "globals.in_trans_ec");
_Static_assert(offsetof(area_$globals_t, pite_in_trans_ec)  == 0x058, "globals.pite_in_trans_ec");
_Static_assert(offsetof(area_$globals_t, seg_table_list)    == 0x068, "globals.seg_table_list");
_Static_assert(offsetof(area_$globals_t, seg_table_pool)    == 0x150, "globals.seg_table_pool");
_Static_assert(offsetof(area_$globals_t, uid_hash_free)     == 0x450, "globals.uid_hash_free");
_Static_assert(offsetof(area_$globals_t, uid_hash)          == 0x454, "globals.uid_hash");
_Static_assert(offsetof(area_$globals_t, uid_hash_pool)     == 0x480, "globals.uid_hash_pool");
_Static_assert(offsetof(area_$globals_t, asid_list)         == 0x4D8, "globals.asid_list");
_Static_assert(offsetof(area_$globals_t, rpmap_seq)         == 0x5C0, "globals.rpmap_seq");
_Static_assert(offsetof(area_$globals_t, next_caller_id)    == 0x5C4, "globals.next_caller_id");
_Static_assert(offsetof(area_$globals_t, free_list)         == 0x5C8, "globals.free_list");
_Static_assert(offsetof(area_$globals_t, partner)           == 0x5CC, "globals.partner");
_Static_assert(offsetof(area_$globals_t, format)            == 0x5D4, "globals.format");
_Static_assert(offsetof(area_$globals_t, del_dup)           == 0x5DC, "globals.del_dup");
_Static_assert(offsetof(area_$globals_t, cr_dup)            == 0x5DE, "globals.cr_dup");
_Static_assert(offsetof(area_$globals_t, n_free)            == 0x5E0, "globals.n_free");
_Static_assert(offsetof(area_$globals_t, n_areas)           == 0x5E2, "globals.n_areas");
_Static_assert(offsetof(area_$globals_t, partner_pkt_size)  == 0x5E4, "globals.partner_pkt_size");
_Static_assert(sizeof(area_$globals_t) == 0x5E8, "AREA_ map segment size = 5E8");
#endif

/*
 * ============================================================================
 * Module Global Variables (fields of AREA_$GLOBALS)
 * ============================================================================
 *
 * The names the SAU2 link map gives interior cells of the block are kept as
 * aliases so callers read the same way the map reads.
 */

/* AREA_$FREE_LIST - head of the free area-entry list (0xE1E6E0, +0x5C8) */
#define AREA_$FREE_LIST         (AREA_$GLOBALS.free_list)

/* AREA_$N_FREE - free entries remaining (0xE1E6F8, +0x5E0) */
#define AREA_$N_FREE            (AREA_$GLOBALS.n_free)

/* AREA_$N_AREAS - highest area ID in use (0xE1E6FA, +0x5E2) */
#define AREA_$N_AREAS           (AREA_$GLOBALS.n_areas)

/* AREA_$PARTNER_PKT_SIZE - partner packet size (0xE1E6FC, +0x5E4) */
#define AREA_$PARTNER_PKT_SIZE  (AREA_$GLOBALS.partner_pkt_size)

/*
 * AREA_$PARTNER - node address of the diskless partner ("mother node"),
 * 0xE1E6E4 (+0x5CC), 8 bytes.
 *
 * AREA_$INIT (0x00E2F426-0x00E2F43C) clears the high longword and stores
 * NETWORK_$MOTHER_NODE into the low longword when NETWORK_$DISKLESS is set,
 * otherwise clears both.  It is always passed BY ADDRESS: `pea (0x5cc,A5)`
 * at 0x00E0792E (REM_FILE_$CREATE_AREA) and 0x00E0795C
 * (NETWORK_$GET_PKT_SIZE), and pmap_$write_page copies both longwords with
 * `move.l (A0)+` at 0x00E13008/0x00E1300C.
 *
 * area_$internal_create tests only the low half (`tst.l (0x5d0,A5)` at
 * 0x00E078E2): a zero low half means "no partner, this node has its own
 * disk".
 */
#define AREA_$PARTNER           (AREA_$GLOBALS.partner)

/*
 * AREA_$NEXT_CALLER_ID - monotonic counter handed out as
 * area_$entry_t.caller_id (0xE1E6DC, +0x5C4).  Cleared by AREA_$INIT at
 * 0x00E2F3C8; read and post-incremented by area_$internal_create at
 * 0x00E078A6/0x00E078AC.
 */
#define AREA_$NEXT_CALLER_ID    (AREA_$GLOBALS.next_caller_id)

/* AREA_$UID_HASH_FREE - head of the free UID hash-chain records (+0x450) */
#define AREA_$UID_HASH_FREE     (AREA_$GLOBALS.uid_hash_free)

/* AREA_$UID_HASH - remote-UID hash table (+0x454), 11 buckets, indexed by
 * M$OIU$WLW(remote_uid, AREA_UID_HASH_BUCKETS) */
#define AREA_$UID_HASH          (AREA_$GLOBALS.uid_hash)

/* AREA_$UID_HASH_POOL - storage for the 11 hash-chain records (+0x480) */
#define AREA_$UID_HASH_POOL     (AREA_$GLOBALS.uid_hash_pool)

/* AREA_$RPMAP_IN_TRANS_EC - map symbol 0xE1E150 (+0x038) */
#define AREA_$RPMAP_IN_TRANS_EC (AREA_$GLOBALS.rpmap_in_trans_ec)

/* AREA_$IN_TRANS_EC - map symbol 0xE1E160 (+0x048) */
#define AREA_$IN_TRANS_EC       (AREA_$GLOBALS.in_trans_ec)

/* AREA_$PITE_IN_TRANS_EC - map symbol 0xE1E170 (+0x058) */
#define AREA_$PITE_IN_TRANS_EC  (AREA_$GLOBALS.pite_in_trans_ec)

/* AREA_$CR_DUP / AREA_$DEL_DUP - dedup counters (0xE1E6F6 / 0xE1E6F4) */
#define AREA_$CR_DUP            (AREA_$GLOBALS.cr_dup)
#define AREA_$DEL_DUP           (AREA_$GLOBALS.del_dup)

/* AREA_$FORMAT - map symbol 0xE1E6EC (+0x5D4) */
#define AREA_$FORMAT            (AREA_$GLOBALS.format)

/*
 * AREA_$ASID_LIST - per-ASID area list heads (0xE1E5F0, +0x4D8).
 * AREA_$INIT clears 58 longwords here (moveq #0x39 + dbf at 0x00E2F3CE).
 * area_$internal_create indexes it with `lsl.w #0x2` on the ASID and
 * `lea (0x0,A5,D0w*0x1)` at 0x00E0785A-0x00E07862, i.e. a *word* scale.
 */
#define AREA_ASID_LIST_BASE     (AREA_GLOBALS_BASE + 0x4D8)
#define AREA_$ASID_LIST         (AREA_$GLOBALS.asid_list)

/*
 * ============================================================================
 * Public Functions
 * ============================================================================
 */

/*
 * AREA_$INIT - Initialize the area subsystem
 *
 * Called during system startup. Initializes:
 *   - Free list of area entries
 *   - Per-ASID area list heads
 *   - UID hash table for deduplication
 *   - Diskless node support structures
 *
 * Original address: 0x00E2F3A8
 */
void AREA_$INIT(void);

/*
 * AREA_$CREATE - Create a new area in current address space
 *
 * Creates a new area with the specified virtual and committed sizes.
 *
 * @param virt_size     Virtual size in bytes (rounded up to 32KB)
 * @param commit_size   Initial committed size in bytes (rounded up to 1KB)
 * @param shared        Domain boolean; true (0xFF) sets AREA_FLAG_REVERSED
 * @param status_p      Output: status code
 *
 * Returns: Area handle (generation << 16 | area_id)
 *
 * Original address: 0x00E079C0
 */
area_$handle_t AREA_$CREATE(uint32_t virt_size, uint32_t commit_size,
                            boolean shared, status_$t *status_p);

/*
 * AREA_$CREATE_FROM - Create area from remote UID (deduplicating)
 *
 * Creates a new area backed by a remote UID. If an area already exists
 * for the same UID and caller_id, returns a reference to the existing
 * area instead of creating a new one.
 *
 * @param remote_uid    Remote object UID
 * @param virt_size     Virtual size in bytes
 * @param commit_size   Committed size in bytes
 * @param caller_id     Caller-provided unique ID for dedup matching
 * @param status_p      Output: status code
 *
 * Returns: Area ID
 *
 * Original address: 0x00E07A02
 */
uint16_t AREA_$CREATE_FROM(uint32_t remote_uid, uint32_t virt_size,
                           uint32_t commit_size, int32_t caller_id,
                           status_$t *status_p);

/*
 * AREA_$DELETE - Delete an area
 *
 * Deletes the specified area, freeing all associated resources.
 * Caller must own the area.
 *
 * @param handle        Area handle
 * @param status_ret    Output: status code
 *
 * Original address: 0x00E07C22
 */
void AREA_$DELETE(area_$handle_t handle, status_$t *status_ret);

/*
 * AREA_$DELETE_FROM - Delete area with specific caller context
 *
 * @param area_index    Area table index (1-based, uint16_t)
 * @param remote_uid    Remote UID to match against entry->remote_uid
 * @param caller_id     Caller ID to match against entry->caller_id
 * @param status_ret    Output: status code
 *
 * Original address: 0x00E07D06
 */
void AREA_$DELETE_FROM(uint16_t area_index, uint32_t remote_uid,
                       uint32_t caller_id, status_$t *status_ret);

/*
 * AREA_$FREE_ASID - Free all areas owned by an address space
 *
 * Called when an address space is being destroyed.
 * Frees all areas owned by the specified ASID.
 *
 * @param asid          Address space ID
 *
 * Original address: 0x00E07E80
 */
void AREA_$FREE_ASID(int16_t asid);

/*
 * AREA_$SHUTDOWN - Shutdown area subsystem
 *
 * Called during system shutdown to release area resources.
 *
 * Original address: 0x00E07F0E
 */
void AREA_$SHUTDOWN(void);

/*
 * AREA_$FREE_FROM - Free every area created from one remote UID
 *
 * Deletes all area entries chained off the UID hash record for
 * `remote_uid`, returns them to the free list, and returns the hash
 * record itself to AREA_$UID_HASH_FREE.
 *
 * @param remote_uid    the value stored in area_$entry_t.remote_uid (+0x20)
 *
 * Original address: 0x00E07FC6
 */
void AREA_$FREE_FROM(uint32_t remote_uid);

/*
 * AREA_$TRANSFER - Transfer area ownership to another address space
 *
 * Transfers ownership of an area to a new address space.
 * Also updates the area's virtual size.
 *
 * @param handle_ptr    Pointer to area handle
 * @param new_asid      New owner ASID
 * @param new_seg_idx   New segment index
 * @param new_virt_size New virtual size
 * @param status_ret    Output: status code
 *
 * Returns: Previous segment index
 *
 * Original address: 0x00E08098
 */
int16_t AREA_$TRANSFER(area_$handle_t *handle_ptr, int16_t new_asid,
                       int16_t new_seg_idx, uint32_t new_virt_size,
                       status_$t *status_ret);

/*
 * AREA_$GROW - Grow an area's virtual size
 *
 * Increases the virtual size of an area. May allocate additional
 * segments and backing store.
 *
 * @param gen           Area generation
 * @param area_id       Area ID
 * @param virt_size     New virtual size
 * @param commit_size   New committed size
 * @param status_ret    Output: status code
 *
 * Original address: 0x00E08BE8
 */
void AREA_$GROW(int16_t gen, uint16_t area_id, uint32_t virt_size,
                uint32_t commit_size, status_$t *status_ret);

/*
 * AREA_$GROW_TO - Grow area to specified size (remote variant)
 *
 * @param area_index    Area table index (1-based, uint16_t)
 * @param virt_size     New virtual size in bytes
 * @param commit_size   New committed size in bytes
 * @param status_ret    Output: status code
 *
 * Original address: 0x00E08CEA
 */
void AREA_$GROW_TO(uint16_t area_index, uint32_t virt_size,
                   uint32_t commit_size, status_$t *status_ret);

/*
 * AREA_$INVALIDATE - Invalidate area pages
 *
 * Invalidates pages within the specified range of an area.
 * This is used to discard pages that are no longer needed.
 *
 * For normal (non-reversed) areas, pages are invalidated from
 * the specified offset forward.
 *
 * For reversed areas (stack-like), the invalidation logic is
 * adjusted to handle the reversed page ordering.
 *
 * Parameters:
 *   gen         - Area generation
 *   area_id     - Area ID
 *   seg_idx     - Segment index
 *   page_offset - Page offset within segment
 *   count       - Number of pages to invalidate
 *   param_6     - BYTE at A6+0x14 (high half of its word slot; MST_$INVALIDATE
 *                 pushes its zero-fill flag with `move.b (-0x3a,A6),-(SP)` at
 *                 0x00E4447A); the body never reads it
 *   status_ret  - Output: status code
 *
 * Original address: 0x00E08DD0
 */
void AREA_$INVALIDATE(int16_t gen, uint16_t area_id, uint16_t seg_idx,
                      uint16_t page_offset, uint32_t count,
                      boolean param_6, status_$t *status_ret);

/*
 * AREA_$COPY - Copy an area (copy-on-write)
 *
 * Creates a copy of an area with copy-on-write semantics.
 * Both source and destination share pages until written.
 *
 * @param gen           Source area generation
 * @param area_id       Source area ID
 * @param new_asid      New owner ASID for copy
 * @param param_4       Unknown parameter
 * @param stack_limit   Stack limit for copy
 * @param status_ret    Output: status code
 *
 * Returns: New area ID
 *
 * Original address: 0x00E0901A
 */
uint32_t AREA_$COPY(int16_t gen, uint16_t area_id, int16_t new_asid,
                    int16_t param_4, uint32_t stack_limit,
                    status_$t *status_ret);

/*
 * AREA_$TOUCH - Touch area pages (bring into memory)
 *
 * Ensures pages within the specified range are in memory.
 *
 * @param handle_ptr    Pointer to area handle
 * @param bste_idx      BSTE index (A6+0x0C)
 * @param seg_idx       Segment index (A6+0x0E)
 * @param param_4       Unknown parameter (A6+0x10)
 * @param ppn_array     PPN list handed straight to AST_$TOUCH_AREA
 *                      (A6+0x12; 0x00E0965A pushes it as the fifth argument
 *                      of the 0x00E03548 call, where it is dereferenced)
 * @param status_p      Output: status code
 *
 * Original address: 0x00E094FE
 */
void AREA_$TOUCH(area_$handle_t *handle_ptr, uint16_t bste_idx,
                 uint16_t seg_idx, int16_t param_4, uint32_t *ppn_array,
                 status_$t *status_p);

/*
 * AREA_$ASSOC - Associate one area page with a physical page
 *
 * Five Pascal parameters; the prologue at 0x00E096B0/0x00E096B4 and the
 * two calls it makes fix the shape (see area/touch.c for the walk).
 *
 * @param area_id       A6+0x08 word - area id, range-checked against
 *                      AREA_$N_AREAS
 * @param bste_idx      A6+0x0A word - area-relative block index, passed BY
 *                      ADDRESS to area_$find_entry_by_uid (`pea (0xa,A6)`)
 * @param page          A6+0x0C word - page within that block
 * @param ppn           A6+0x0E long - forwarded to AST_$ASSOC_AREA
 * @param status_ret    A6+0x12 long - Output: status code
 *
 * Original address: 0x00E096A2
 */
void AREA_$ASSOC(int16_t area_id, uint16_t bste_idx, int16_t page,
                 uint32_t ppn, status_$t *status_ret);

/*
 * AREA_$THREAD_BSTES - Thread BSTE entries for area
 *
 * Links BSTE (Backing Store Table Entry) entries for the area.
 *
 * @param handle_ptr    Pointer to area handle
 * @param bste_idx      First BSTE index
 * @param seg_idx       First segment index
 * @param param_4       Unknown parameter
 * @param status_ret    Output: status code
 *
 * Original address: 0x00E09722
 */
void AREA_$THREAD_BSTES(area_$handle_t *handle_ptr, int16_t bste_idx,
                        int16_t seg_idx, uint32_t param_4,
                        status_$t *status_ret);

/*
 * AREA_$REMOVE_SEG - Remove a segment from an area
 *
 * Six Pascal parameters; see area/segment.c for the recovered shape and
 * for the call site at 0x00E449F8 that proves it.
 *
 * @param seg_rec       record whose +0x02 is the area id
 * @param arg_0c        compared against the area entry's +0x26
 * @param arg_0e        added into the segment-count comparison
 * @param arg_10        boolean gating the "delete the whole area" path
 * @param arg_12        segment index / count
 * @param status_ret    Output: status code
 *
 * Original address: 0x00E09822
 */
void AREA_$REMOVE_SEG(void *seg_rec, uint16_t arg_0c, uint16_t arg_0e,
                      int8_t arg_10, uint16_t arg_12,
                      status_$t *status_ret);

/*
 * AREA_$DEACTIVATE_ASTE - Deactivate AST entry for area
 *
 * @param aste          Pointer to AST entry
 * @param status_ret    Output: status code
 *
 * Original address: 0x00E09EF4
 */
void AREA_$DEACTIVATE_ASTE(void *aste, status_$t *status_ret);

/*
 * ============================================================================
 * Internal Functions (declared for cross-module use)
 * ============================================================================
 */

/*
 * area_$wait_in_trans - Wait for area in-transition to complete
 *
 * Called when AREA_FLAG_IN_TRANS is set to wait for the
 * current operation to complete.
 *
 * Original address: 0x00E07742
 */
void area_$wait_in_trans(void);

/*
 * area_$internal_delete - Internal area deletion
 *
 * Core deletion logic used by public deletion functions.
 *
 * @param entry         Area entry pointer
 * @param area_id       Area ID
 * @param status_p      Output: status code
 * @param do_unlink     If negative, unlink from ASID list
 *
 * Original address: 0x00E07B50
 */
void area_$internal_delete(area_$entry_t *entry, int16_t area_id,
                           status_$t *status_p, boolean do_unlink);

/*
 * area_$internal_create - Internal area creation
 *
 * Core creation logic used by public creation functions.
 *
 * @param virt_size     Virtual size
 * @param commit_size   Committed size
 * @param remote_uid    Remote UID (0 for local)
 * @param owner_asid    Owner ASID
 * @param alloc_remote  Allocate remote backing
 * @param shared        Shared flag
 * @param status_p      Output: status code
 *
 * Returns: Area handle
 *
 * Original address: 0x00E077DA
 */
uint32_t area_$internal_create(uint32_t virt_size, uint32_t commit_size,
                               uint32_t remote_uid, int16_t owner_asid,
                               int16_t alloc_remote, boolean shared,
                               status_$t *status_p);

/*
 * area_$resize - Resize an area
 *
 * Core resize logic for grow/shrink operations.
 *
 * @param area_id       Area ID
 * @param entry         Area entry pointer
 * @param virt_size     New virtual size
 * @param commit_size   New committed size
 * @param is_grow       1 for grow, 0 for initial create
 * @param status_p      Output: status code
 *
 * Original address: 0x00E08816
 */
void area_$resize(int16_t area_id, area_$entry_t *entry,
                  uint32_t virt_size, uint32_t commit_size,
                  int16_t is_grow, status_$t *status_p);

#endif /* AREA_H */
