/*
 * MMAP - Memory Map Management
 *
 * This module manages the physical memory map and working set lists (WSL).
 * Each physical page has an entry in the mmape_t array, and each process
 * has a working set list tracking which pages it has in memory.
 *
 * Key data structures:
 * - mmape_t: Per-physical-page entry (16 bytes each)
 * - ws_hdr_t: Working set list header (36 bytes each)
 *
 * Key addresses (m68k):
 * - MMAP global data: 0xE23284
 * - MMAP_$WSL (ws_hdr_t array): 0xE232B0
 * - MMAP_$WSL_HI_MARK (pid-to-wsl map): 0xE23CA6
 * - mmape_t array MMAP_$MMAPE: 0xEB4800 (ppn 0x200; ppn 0 would be 0xEB2800)
 *
 * Original source was likely Pascal, converted to C.
 */

#ifndef MMAP_H
#define MMAP_H

#include "base/base.h"
#include "ml/ml.h"

/* MMAP status codes (module 0x06) */
#define status_$mmap_bad_avail 0x00060004
#define status_$mmap_inconsistent_mmape 0x00060008
#define status_$mmap_illegal_wsl_index 0x00060009
#define status_$mmap_illegal_pid 0x0006000a
#define status_$mmap_ws_lists_exhausted 0x0006000b
#define status_$mmap_bad_reclaim 0x0006000d
#define status_$mmap_contig_pages_unavailable 0x0006000e

/* Forward declarations */
struct mmape_t;
struct ws_hdr_t;

/*
 * Memory Map Page Entry (mmape_t)
 *
 * One entry exists for each pageable physical page (0x200..0xFFF), in the
 * table MMAP_$MMAPE at 0xEB4800 (entry for ppn at 0xEB2800 + ppn*16); reach
 * one with MMAPE_FOR_VPN(ppn).
 *
 * Pages are linked together in doubly-linked lists per working set list.
 */
typedef struct mmape_t {
  uint8_t wire_count; /* 0x00: Wire count (prevents paging when > 0) */
  uint8_t seg_offset; /* 0x01: Segment offset / page offset in segment */
  uint16_t segment;   /* 0x02: Segment index (<<7 for segment base) */
  uint8_t wsl_index;  /* 0x04: Working set list index this page belongs to */
  uint8_t flags1;     /* 0x05: Flags - see MMAPE_FLAG1_* below */
  uint16_t prev_vpn;  /* 0x06: Previous page in WSL doubly-linked list */
  uint8_t priority;   /* 0x08: Page priority for replacement */
  uint8_t flags2;     /* 0x09: Flags - see MMAPE_FLAG2_* below */
  uint16_t next_vpn;  /* 0x0A: Next page in WSL doubly-linked list */
  uint32_t disk_addr; /* 0x0C: Disk address (used by AST layer for paging) */
} mmape_t;

/* Remaining documented offsets (bead source-pewa). */
#if defined(ARCH_M68K)
_Static_assert(__builtin_offsetof(mmape_t, wire_count) == 0x00, "mmape_t.wire_count");
_Static_assert(__builtin_offsetof(mmape_t, prev_vpn) == 0x06, "mmape_t.prev_vpn");
_Static_assert(__builtin_offsetof(mmape_t, next_vpn) == 0x0A, "mmape_t.next_vpn");
#endif

/* mmape_t flags1 bit definitions */
#define MMAPE_FLAG1_IN_WSL 0x80 /* Page is installed in a working set list */
#define MMAPE_FLAG1_IMPURE                                                     \
  0x40 /* Page is impure (writable/data) vs pure (code) */

/* mmape_t flags2 bit definitions */
#define MMAPE_FLAG2_ON_DISK 0x80  /* Page has backing store / needs paging */
#define MMAPE_FLAG2_MODIFIED 0x40 /* Page has been modified (dirty) */

/*
 * Working Set List Header (ws_hdr_t)
 *
 * Each process has a working set list that tracks which pages it has
 * resident in memory. WSL indices 0-4 are special (0 = free pool, 5 = wired).
 * User processes use indices 5-69.
 */
typedef struct ws_hdr_t {
  uint8_t flags;          /* 0x00: Flags - bit 7 = in_use */
  uint8_t reserved1;      /* 0x01: Reserved */
  uint16_t owner;         /* 0x02: Owner field (process/subsystem) */
  uint32_t page_count;    /* 0x04: Number of pages in this WSL */
  uint32_t scan_pos;      /* 0x08: Scan position for page replacement */
  uint32_t head_vpn;      /* 0x0C: Head of page list (VPN) */
  uint32_t max_pages;     /* 0x10: Maximum pages allowed */
  uint32_t field_14;      /* 0x14: Unknown field */
  uint32_t pri_timestamp; /* 0x18: Priority update timestamp */
  uint32_t ws_timestamp;  /* 0x1C: Working set timestamp */
  /*
   * 0x20: working-set floor.  PMAP_$PURIFIER_L only considers a working
   * set as a steal candidate while page_count > ws_floor
   * (0x00E13EB0-0x00E13EBE and 0x00E13F1A-0x00E13F28).
   */
  uint32_t ws_floor;
} ws_hdr_t;

/* Remaining documented offsets (bead source-pewa). */
#if defined(ARCH_M68K)
_Static_assert(__builtin_offsetof(ws_hdr_t, flags) == 0x00, "ws_hdr_t.flags");
_Static_assert(__builtin_offsetof(ws_hdr_t, reserved1) == 0x01, "ws_hdr_t.reserved1");
#endif

/*
 * The WSL array stride is 0x24 (36) bytes, not 0x28: PMAP_$PURIFIER_L walks
 * it with `lea (-0x24,A1),A1` (0x00E13EC2) and the per-pool page counts are
 * 0x24 apart (0xE232B4, 0xE232D8, 0xE232FC, 0xE23320, 0xE23344, 0xE23368).
 */
#if defined(ARCH_M68K)
_Static_assert(__builtin_offsetof(ws_hdr_t, owner) == 0x02, "ws_hdr_t.owner");
_Static_assert(__builtin_offsetof(ws_hdr_t, page_count) == 0x04,
               "ws_hdr_t.page_count (0xE232B4 for WSL[0])");
_Static_assert(__builtin_offsetof(ws_hdr_t, scan_pos) == 0x08, "ws_hdr_t.scan_pos");
_Static_assert(__builtin_offsetof(ws_hdr_t, head_vpn) == 0x0C, "ws_hdr_t.head_vpn");
_Static_assert(__builtin_offsetof(ws_hdr_t, max_pages) == 0x10, "ws_hdr_t.max_pages");
_Static_assert(__builtin_offsetof(ws_hdr_t, field_14) == 0x14, "ws_hdr_t.field_14");
_Static_assert(__builtin_offsetof(ws_hdr_t, pri_timestamp) == 0x18,
               "ws_hdr_t.pri_timestamp");
_Static_assert(__builtin_offsetof(ws_hdr_t, ws_timestamp) == 0x1C,
               "ws_hdr_t.ws_timestamp");
_Static_assert(__builtin_offsetof(ws_hdr_t, ws_floor) == 0x20, "ws_hdr_t.ws_floor");
_Static_assert(sizeof(ws_hdr_t) == 0x24, "ws_hdr_t stride must be 0x24 bytes");

_Static_assert(__builtin_offsetof(mmape_t, seg_offset) == 0x01, "mmape_t.seg_offset");
_Static_assert(__builtin_offsetof(mmape_t, segment) == 0x02, "mmape_t.segment");
_Static_assert(__builtin_offsetof(mmape_t, wsl_index) == 0x04, "mmape_t.wsl_index");
_Static_assert(__builtin_offsetof(mmape_t, flags1) == 0x05, "mmape_t.flags1");
_Static_assert(__builtin_offsetof(mmape_t, priority) == 0x08, "mmape_t.priority");
_Static_assert(__builtin_offsetof(mmape_t, flags2) == 0x09, "mmape_t.flags2");
_Static_assert(__builtin_offsetof(mmape_t, disk_addr) == 0x0C, "mmape_t.disk_addr");
_Static_assert(sizeof(mmape_t) == 0x10, "mmape_t stride must be 0x10 bytes");
#endif

/* WSL flags bit definitions */
#define WSL_FLAG_IN_USE 0x80 /* Working set list is in use */

/* WSL special indices */
#define WSL_INDEX_FREE_POOL 0 /* Free page pool */
#define WSL_INDEX_WIRED 5     /* Wired/locked pages */

/*
 * Working-set list indices 0..5 are the six global page pools.  The index
 * is the same value that is stored in mmape_t.wsl_index (the
 * MMAP_PAGE_TYPE_* codes).  MMAP_$WSL[n].page_count is the per-pool page
 * count that the purifiers and AST_$ALLOCATE_PAGES test; the addresses are
 * the ones Ghidra labels MMAP_$WSL_FREE_CNT .. MMAP_$WSL_WIRED_CNT.
 */
#define MMAP_WSL_POOL_FREE 0        /* 0xE232B4: free pages */
#define MMAP_WSL_POOL_PURE 1        /* 0xE232D8: clean read-only pages */
#define MMAP_WSL_POOL_IMPURE 2      /* 0xE232FC: clean writable pages */
#define MMAP_WSL_POOL_DIRTY_LOCAL 3 /* 0xE23320: dirty, PMAP_$PURIFIER_L */
#define MMAP_WSL_POOL_DIRTY_RMT 4   /* 0xE23344: dirty, PMAP_$PURIFIER_R */
#define MMAP_WSL_POOL_WIRED 5       /* 0xE23368: wired pages */
#define WSL_INDEX_MIN_USER 5  /* Minimum user WSL index */
#define WSL_INDEX_MAX 69      /* Maximum WSL index (0x45) */

/*
 * MMAP_$WSL holds WSL_INDEX_MAX + 1 records: the map runs the object from
 * 0xE232B0 to MMAP_$MIN_RMT_POOL at 0xE23C88, 0x9D8 bytes / 0x24 = 70.
 */
#define MMAP_WSL_SLOTS 70

/*
 * MMAP_$WS_OWNER holds 64 words: the map runs it from 0xE23CA8 to
 * MMAP_$RMT_LIMIT at 0xE23D28, 0x80 bytes.  MMAP_$INIT clears entries 1..63
 * with a `dbf' on `moveq #0x3e' and then stores 7 into entry 0
 * (0x00E31946-0x00E3195A).
 */
#define MMAP_WS_OWNER_SLOTS 64

/* Maximum PID for pid-to-wsl mapping */
#define MMAP_MAX_PID 64 /* 0x40 */

/* Page type codes for mmap_$add_to_wsl (0x00e0c514, add page to WSL) */
#define MMAP_PAGE_TYPE_FREE 0     /* Free/available page */
#define MMAP_PAGE_TYPE_PURE 1     /* Pure (code) page */
#define MMAP_PAGE_TYPE_IMPURE 2   /* Impure (data) page - not modified */
#define MMAP_PAGE_TYPE_DIRTY_NF 3 /* Dirty page, no flush needed */
#define MMAP_PAGE_TYPE_DIRTY_FL 4 /* Dirty page, needs flush */

/*
 * ============================================================================
 * The MMAP_ module data block
 * ============================================================================
 *
 * `D E23284 MMAP_ size = AA8` in the SAU2 map - 0xE23284..0xE23D2C - is one
 * Domain Pascal module data block, reached through a single A5 base: e.g.
 * MMAP_$FREE (0x00E0CACA) does `lea (0xe23284).l,A5` and then names each
 * cell as a displacement off A5.  The map's interior symbols name every
 * field below except offset 0x000, and MMAP_$FREE's `pea (A5)` into
 * ML_$SPIN_LOCK (0x00E0CAE2/0x00E0CB0E) shows that offset 0x000 is the
 * module's spin lock.
 *
 *   +0x000  0xE23284  (the cell ML_$SPIN_LOCK/ML_$SPIN_UNLOCK are handed)
 *   +0x004  0xE23288  MMAP_$HI_INDX
 *   +0x008  0xE2328C  MMAP_$LO_INDX
 *   +0x00C  0xE23290  MMAP_$WS_REMOVE
 *   +0x010  0xE23294  MMAP_$RECLAIM_PUR_CNT
 *   +0x014  0xE23298  MMAP_$RECLAIM_SHAR_CNT
 *   +0x018  0xE2329C  MMAP_$WS_SCAN_CNT
 *   +0x01C  0xE232A0  MMAP_$WS_OVERFLOW
 *   +0x020  0xE232A4  MMAP_$STEAL_CNT
 *   +0x024  0xE232A8  MMAP_$ALLOC_PAGES
 *   +0x028  0xE232AC  MMAP_$ALLOC_CNT
 *   +0x02C  0xE232B0  MMAP_$WSL                 70 x ws_hdr_t (0x9D8)
 *   +0xA04  0xE23C88  MMAP_$MIN_RMT_POOL
 *   +0xA08  0xE23C8C  MMAP_$HPPN
 *   +0xA0C  0xE23C90  MMAP_$LPPN
 *   +0xA10  0xE23C94  MMAP_$PAGEABLE_PAGES_LOWER_LIMIT
 *   +0xA14  0xE23C98  MMAP_$PAGEABLE_PAGES
 *   +0xA18  0xE23C9C  MMAP_$REMOTE_PAGES
 *   +0xA1C  0xE23CA0  MMAP_$REAL_PAGES
 *   +0xA20  0xE23CA4  MMAP_$FORMAT              (word)
 *   +0xA22  0xE23CA6  MMAP_$WSL_HI_MARK         (word)
 *   +0xA24  0xE23CA8  MMAP_$WS_OWNER            64 words (0x80)
 *   +0xAA4  0xE23D28  MMAP_$RMT_LIMIT
 *
 * Image contents (`gsk read 0x00E23284` / `gsk read 0x00E23C88`): everything
 * is zero except MMAP_$HI_INDX = 0xFFF, MMAP_$LO_INDX = 0x200,
 * MMAP_$MIN_RMT_POOL = 0x42, MMAP_$HPPN = 1, MMAP_$LPPN = 0xFFF,
 * MMAP_$PAGEABLE_PAGES_LOWER_LIMIT = 0x42, MMAP_$FORMAT = 1 and
 * MMAP_$WSL_HI_MARK = 8.  mmap/mmap_data.c carries those seeds.
 */
typedef struct mmap_globals_t {
  /*
   * +0x000 - the module spin lock.  Unnamed in the map; every MMAP_ entry
   * point that takes it passes the bare A5 base (`pea (A5)`).
   */
  uint32_t lock;

  /*
   * +0x004 / +0x008 - MMAP_$HI_INDX / MMAP_$LO_INDX.  Seeded in the image
   * (0xFFF and 0x200) and read by nothing in this build: `gsk xrefs to
   * 00E23288` and `00E2328C` are both empty.
   */
  uint32_t hi_indx;
  uint32_t lo_indx;

  /* +0x00C - pages removed from working sets by the WS scanner */
  uint32_t ws_remove;

  /* +0x010 / +0x014 - reclaim counters reported by OSINFO_$GET_MMAP */
  uint32_t reclaim_pur_cnt;
  uint32_t reclaim_shar_cnt;

  /* +0x018 - working-set scan passes */
  uint32_t ws_scan_cnt;

  /* +0x01C - working sets that exceeded their maximum */
  uint32_t ws_overflow;

  /* +0x020 / +0x024 / +0x028 - allocator counters */
  uint32_t steal_cnt;
  uint32_t alloc_pages;
  uint32_t alloc_cnt;

  /*
   * +0x02C - MMAP_$WSL, the 70 working-set list headers.  The map runs the
   * object from 0xE232B0 to MMAP_$MIN_RMT_POOL at 0xE23C88, i.e. 0x9D8
   * bytes = 70 records of 0x24.
   */
  ws_hdr_t wsl[MMAP_WSL_SLOTS];

  /*
   * +0xA04 - MMAP_$MIN_RMT_POOL, a longword: NETWORK_$PAGE_SERVER does
   * `add.l D1,(0x00e23c88).l` at 0x00E11578 and then copies it into
   * pageable_pages_lower_limit at 0x00E1157E.
   */
  uint32_t min_rmt_pool;

  /*
   * +0xA08 / +0xA0C - the highest and lowest pageable page numbers.
   * MMAP_$INIT narrows them from the image seeds (HPPN = 1, LPPN = 0xFFF)
   * as it walks the PMAP entries (mmap/init.c).
   */
  uint32_t hppn;
  uint32_t lppn;

  /*
   * +0xA10 - the floor PMAP's purifiers keep the pageable-page count above.
   * Written at 0x00E1157E and 0x00E11910; image value 0x42.
   */
  uint32_t pageable_pages_lower_limit;

  /* +0xA14 - pages currently pageable (counted up by MMAP_$INIT) */
  uint32_t pageable_pages;

  /* +0xA18 / +0xA1C - remote and real page totals */
  uint32_t remote_pages;
  uint32_t real_pages;

  /*
   * +0xA20 - MMAP_$FORMAT, a word (MMAP_$WSL_HI_MARK follows two bytes
   * later).  Image value 1; `gsk xrefs to 00E23CA4` is empty.
   */
  uint16_t format;

  /*
   * +0xA22 - MMAP_$WSL_HI_MARK, the highest WSL slot handed out so far.
   * Image value 8.
   */
  uint16_t wsl_hi_mark;

  /*
   * +0xA24 - MMAP_$WS_OWNER, the WSL index in use by each process, 64
   * words (the map runs it to MMAP_$RMT_LIMIT at 0xE23D28).  Every indexed
   * reader biases the base by -2 and uses a 1-based index, so C code spells
   * that MMAP_$WS_OWNER[n - 1]; MMAP_PID_TO_WSL below is that biased base.
   */
  uint16_t ws_owner[MMAP_WS_OWNER_SLOTS];

  /*
   * +0xAA4 - MMAP_$RMT_LIMIT, the last longword of the block.  Its only two
   * readers, NETWORK_$PAGE_SERVER at 0x00E11598 and 0x00E1192A, do
   * `tst.b (0x00e23d28).l / bpl`, i.e. they test the sign of the
   * most-significant byte, which is the sign of the big-endian longword.
   */
  uint32_t rmt_limit;
} mmap_globals_t;

/*
 * Layout recovered from the SAU2 map's interior symbols (addresses above)
 * plus the accessing instructions quoted in the field comments.  The block
 * holds no pointers, so these hold on every target.
 */
_Static_assert(__builtin_offsetof(mmap_globals_t, lock) == 0x000,
               "mmap_globals_t.lock (0xE23284)");
_Static_assert(__builtin_offsetof(mmap_globals_t, hi_indx) == 0x004,
               "mmap_globals_t.hi_indx (0xE23288 MMAP_$HI_INDX)");
_Static_assert(__builtin_offsetof(mmap_globals_t, lo_indx) == 0x008,
               "mmap_globals_t.lo_indx (0xE2328C MMAP_$LO_INDX)");
_Static_assert(__builtin_offsetof(mmap_globals_t, ws_remove) == 0x00C,
               "mmap_globals_t.ws_remove (0xE23290 MMAP_$WS_REMOVE)");
_Static_assert(__builtin_offsetof(mmap_globals_t, reclaim_pur_cnt) == 0x010,
               "mmap_globals_t.reclaim_pur_cnt (0xE23294)");
_Static_assert(__builtin_offsetof(mmap_globals_t, reclaim_shar_cnt) == 0x014,
               "mmap_globals_t.reclaim_shar_cnt (0xE23298)");
_Static_assert(__builtin_offsetof(mmap_globals_t, ws_scan_cnt) == 0x018,
               "mmap_globals_t.ws_scan_cnt (0xE2329C)");
_Static_assert(__builtin_offsetof(mmap_globals_t, ws_overflow) == 0x01C,
               "mmap_globals_t.ws_overflow (0xE232A0)");
_Static_assert(__builtin_offsetof(mmap_globals_t, steal_cnt) == 0x020,
               "mmap_globals_t.steal_cnt (0xE232A4)");
_Static_assert(__builtin_offsetof(mmap_globals_t, alloc_pages) == 0x024,
               "mmap_globals_t.alloc_pages (0xE232A8)");
_Static_assert(__builtin_offsetof(mmap_globals_t, alloc_cnt) == 0x028,
               "mmap_globals_t.alloc_cnt (0xE232AC)");
_Static_assert(__builtin_offsetof(mmap_globals_t, wsl) == 0x02C,
               "mmap_globals_t.wsl (0xE232B0 MMAP_$WSL)");
_Static_assert(__builtin_offsetof(mmap_globals_t, min_rmt_pool) == 0xA04,
               "mmap_globals_t.min_rmt_pool (0xE23C88)");
_Static_assert(__builtin_offsetof(mmap_globals_t, hppn) == 0xA08,
               "mmap_globals_t.hppn (0xE23C8C MMAP_$HPPN)");
_Static_assert(__builtin_offsetof(mmap_globals_t, lppn) == 0xA0C,
               "mmap_globals_t.lppn (0xE23C90 MMAP_$LPPN)");
_Static_assert(__builtin_offsetof(mmap_globals_t, pageable_pages_lower_limit)
                   == 0xA10,
               "mmap_globals_t.pageable_pages_lower_limit (0xE23C94)");
_Static_assert(__builtin_offsetof(mmap_globals_t, pageable_pages) == 0xA14,
               "mmap_globals_t.pageable_pages (0xE23C98)");
_Static_assert(__builtin_offsetof(mmap_globals_t, remote_pages) == 0xA18,
               "mmap_globals_t.remote_pages (0xE23C9C)");
_Static_assert(__builtin_offsetof(mmap_globals_t, real_pages) == 0xA1C,
               "mmap_globals_t.real_pages (0xE23CA0)");
_Static_assert(__builtin_offsetof(mmap_globals_t, format) == 0xA20,
               "mmap_globals_t.format (0xE23CA4 MMAP_$FORMAT)");
_Static_assert(__builtin_offsetof(mmap_globals_t, wsl_hi_mark) == 0xA22,
               "mmap_globals_t.wsl_hi_mark (0xE23CA6)");
_Static_assert(__builtin_offsetof(mmap_globals_t, ws_owner) == 0xA24,
               "mmap_globals_t.ws_owner (0xE23CA8 MMAP_$WS_OWNER)");
_Static_assert(__builtin_offsetof(mmap_globals_t, rmt_limit) == 0xAA4,
               "mmap_globals_t.rmt_limit (0xE23D28 MMAP_$RMT_LIMIT)");
_Static_assert(sizeof(mmap_globals_t) == 0xAA8,
               "MMAP_ block is 0xE23284..0xE23D2C (`D E23284 MMAP_ size = AA8')");

/*
 * The block itself, MMAP_$DATA: the A5 base every MMAP_ entry point loads, a
 * MODULE_DATA block defined in mmap/mmap_data.c (source-702z; the target
 * had an absolute-address macro, the host a separate object).  MMAP_GLOBALS
 * is the name the MMAP code uses for it.
 */
MODULE_DATA_DECLARE(mmap_globals_t, MMAP_$DATA, 0x00E23284);
#define MMAP_GLOBALS MMAP_$DATA

/*
 * MMAP_$MMAPE - the MMAP page table, one mmape_t per physical page.
 * Module data block MMAP_$MMAPE: Claude Opus 5.5 (source-fyjc).
 *
 * SAU2 map: "D EB4800 OS_PMAPS size = 10000" holds MMAP at 0x00EB4800 and
 * MMU_$PTTX at 0x00EC2800, so the table is 0x00EB4800..0x00EC27FF, 0xE000
 * bytes = 0xE00 entries of 0x10.  It is a separate object, not part of the
 * MMAP_ block.  MMAP_$INIT manages ppn 0x200 (MMAP_$LO_INDX) .. 0xFFF
 * (MMAP_$HI_INDX), so it is the Pascal array [0x200..0xFFF] of mmape_t and
 * the compiler folds the lower bound into the displacement: every user
 * loads the table's own address and reaches entry `ppn' 0x2000 bytes below
 * it -
 *   MMAP_$INIT       `movea.l #0xeb4800,A3' / `lea (0x2000,A3),A3' for
 *                    ppn 0x200 (0x00E319C2-0x00E319CE), then
 *                    `move.w (-0x2000,A2),D0w' (0x00E319E8, 0x00E31A5C)
 *   OSINFO_$GET_MMAP `movea.l #0xeb4800,A0' then (-0x1ffb,A0) / (-0x1ffc,A0)
 *                    (0x00E5C7DE-0x00E5C7FA)
 *   netbuf           `movea.l #0xeb4800,A0 / lsl.l #0x4,Dn / lea (0,A0,Dn),A1'
 *                    then (-0x1ffa,A1) (0x00E0EAA2, 0x00E0EADA, ...)
 * - i.e. entry ppn is at 0xEB2800 + ppn*0x10.  Entry 0 would be 0xEB2800,
 * inside the STACK segment (OS_$STACK, os/os.h), so the bias cannot be
 * folded into the block's declaration: like PMAP_SEGMAP_ROW the table is
 * declared from its first element, ppn 0x200, and MMAPE_FOR_VPN(ppn)
 * applies the bias once.
 *
 * The image carries no bytes for OS_PMAPS (the memory probe and MMAP_$INIT
 * fill it at boot), so the block is zero-filled.  OS_$INIT frees the pages
 * of the table that hold no real page by VA (`movea.l #0xeb4800,A3' +
 * (i-1)*0x400, 0x00E340AA-0x00E340C8) and MMAP_$INIT translates them with
 * MMU_$VTOP, so the type is page (0x400) aligned as the image's 0xEB4800 is.
 */
#define MMAP_MMAPE_FIRST_PPN    0x200   /* MMAP_$LO_INDX, the Pascal lower bound */
#define MMAP_MMAPE_LAST_PPN     0xFFF   /* MMAP_$HI_INDX */
#define MMAP_MMAPE_COUNT        (MMAP_MMAPE_LAST_PPN - MMAP_MMAPE_FIRST_PPN + 1)
#define MMAP_$MMAPE_SIZE        0xE000  /* MMAP 0xEB4800 .. MMU_$PTTX 0xEC2800 */

typedef struct __attribute__((aligned(0x400))) mmap_$mmape_table_t {
    mmape_t entry[MMAP_MMAPE_COUNT];    /* entry[0] = ppn 0x200 */
} mmap_$mmape_table_t;

_Static_assert(sizeof(mmape_t) == 0x10, "mmape_t stride 0x10 (lsl.l #0x4)");
_Static_assert(sizeof(mmap_$mmape_table_t) == MMAP_$MMAPE_SIZE,
               "MMAP: 0xEB4800..MMU_$PTTX 0xEC2800 in OS_PMAPS");
_Static_assert(MMAP_MMAPE_FIRST_PPN * sizeof(mmape_t) == 0x2000,
               "the bias: (-0x2000,An) off 0xEB4800 is ppn 0's entry");

MODULE_DATA_DECLARE(mmap_$mmape_table_t, MMAP_$MMAPE, 0x00EB4800);

/*
 * Every separately named cell of the block, as an accessor over the one
 * object.  The names are the SAU2 map's, so users outside mmap/ spell them
 * exactly as the map does.
 */
#define MMAP_$HI_INDX                    (MMAP_GLOBALS.hi_indx)
#define MMAP_$LO_INDX                    (MMAP_GLOBALS.lo_indx)
#define MMAP_$WS_REMOVE                  (MMAP_GLOBALS.ws_remove)
#define MMAP_$RECLAIM_PUR_CNT            (MMAP_GLOBALS.reclaim_pur_cnt)
#define MMAP_$RECLAIM_SHAR_CNT           (MMAP_GLOBALS.reclaim_shar_cnt)
#define MMAP_$WS_SCAN_CNT                (MMAP_GLOBALS.ws_scan_cnt)
#define MMAP_$WS_OVERFLOW                (MMAP_GLOBALS.ws_overflow)
#define MMAP_$STEAL_CNT                  (MMAP_GLOBALS.steal_cnt)
#define MMAP_$ALLOC_PAGES                (MMAP_GLOBALS.alloc_pages)
#define MMAP_$ALLOC_CNT                  (MMAP_GLOBALS.alloc_cnt)
#define MMAP_$WSL                        (MMAP_GLOBALS.wsl)
#define MMAP_$MIN_RMT_POOL               (MMAP_GLOBALS.min_rmt_pool)
#define MMAP_$HPPN                       (MMAP_GLOBALS.hppn)
#define MMAP_$LPPN                       (MMAP_GLOBALS.lppn)
#define MMAP_$PAGEABLE_PAGES_LOWER_LIMIT (MMAP_GLOBALS.pageable_pages_lower_limit)
#define MMAP_$PAGEABLE_PAGES             (MMAP_GLOBALS.pageable_pages)
#define MMAP_$REMOTE_PAGES               (MMAP_GLOBALS.remote_pages)
#define MMAP_$REAL_PAGES                 (MMAP_GLOBALS.real_pages)
#define MMAP_$FORMAT                     (MMAP_GLOBALS.format)
#define MMAP_$WSL_HI_MARK                (MMAP_GLOBALS.wsl_hi_mark)
#define MMAP_$WS_OWNER                   (MMAP_GLOBALS.ws_owner)
#define MMAP_$RMT_LIMIT                  (MMAP_GLOBALS.rmt_limit)

/* The tree's older spellings of two of those cells. */
#define MMAP_WSL         MMAP_$WSL
#define MMAP_WSL_HI_MARK MMAP_$WSL_HI_MARK

/*
 * MMAP_PID_TO_WSL - MMAP_$WS_OWNER reached the way the binary reaches it,
 * through the base biased by -2 and a 1-based index.  OSINFO_$GET_MMAP does
 * `movea.l #0xe23ca8,A3 / move.w (-0x2,A3,D1w*0x1)` with D1 = asid * 2
 * (0x00E5C71C-0x00E5C724), and MMAP_$SET_WS_INDEX and friends address the
 * same words as 0xE23CA6 + pid * 2, so MMAP_PID_TO_WSL[pid] is
 * MMAP_$WS_OWNER[pid - 1] and MMAP_PID_TO_WSL[0] is MMAP_$WSL_HI_MARK.
 */
#define MMAP_PID_TO_WSL ((uint16_t *)&MMAP_GLOBALS.wsl_hi_mark)

/*
 * Per-pool page counts.
 *
 * The SAU2 map has a single object `E232B0 MMAP_$WSL` running to
 * MMAP_$MIN_RMT_POOL at 0xE23C88 - 0x9D8 bytes, i.e. 70 ws_hdr_t records of
 * 0x24 - which is exactly MMAP_WSL.  The six addresses Ghidra labels
 * MMAP_$WSL_*_CNT are the page_count field (record + 0x04) of the six global
 * pools, so they are not separate objects:
 *
 *   0xE232B4 MMAP_$WSL_FREE_CNT         MMAP_WSL[0].page_count
 *   0xE232D8 MMAP_$WSL_PURE_CNT         MMAP_WSL[1].page_count
 *   0xE232FC MMAP_$WSL_IMPURE_CNT       MMAP_WSL[2].page_count
 *   0xE23320 MMAP_$WSL_DIRTY_LOCAL_CNT  MMAP_WSL[3].page_count
 *   0xE23344 MMAP_$WSL_DIRTY_RMT_CNT    MMAP_WSL[4].page_count
 *   0xE23368 MMAP_$WSL_WIRED_CNT        MMAP_WSL[5].page_count
 */
#define MMAP_$WSL_FREE_CNT         (MMAP_WSL[MMAP_WSL_POOL_FREE].page_count)
#define MMAP_$WSL_PURE_CNT         (MMAP_WSL[MMAP_WSL_POOL_PURE].page_count)
#define MMAP_$WSL_IMPURE_CNT       (MMAP_WSL[MMAP_WSL_POOL_IMPURE].page_count)
#define MMAP_$WSL_DIRTY_LOCAL_CNT  (MMAP_WSL[MMAP_WSL_POOL_DIRTY_LOCAL].page_count)
#define MMAP_$WSL_DIRTY_RMT_CNT    (MMAP_WSL[MMAP_WSL_POOL_DIRTY_RMT].page_count)
#define MMAP_$WSL_WIRED_CNT        (MMAP_WSL[MMAP_WSL_POOL_WIRED].page_count)

/*
 * MMAPE_FOR_VPN(ppn) - the mmape_t of physical page `ppn' (0x200..0xFFF),
 * the image's 0xEB2800 + ppn*0x10: the table's Pascal lower bound applied
 * once (see MMAP_$MMAPE above).  The name is the tree's; the index is a
 * physical page number, taken as a signed 32-bit value so a ppn below 0x200
 * forms the address the image would (the image's 32-bit arithmetic) rather
 * than wrapping on a 64-bit host.
 */
#define MMAPE_FOR_VPN(ppn) \
    (&MMAP_$MMAPE.entry[(int32_t)(ppn) - MMAP_MMAPE_FIRST_PPN])

/* Get WSL header for a WSL index */
#define WSL_FOR_INDEX(idx) (&MMAP_WSL[(idx)])

/* Get WSL index for a process ID */
#define WSL_FOR_PID(pid) (MMAP_PID_TO_WSL[(pid)])

/*
 * Note: Internal error status arrays and other internal globals
 * are declared in mmap_internal.h. Include that header in .c files
 * that need access to internal MMAP data.
 */

/*
 * Function prototypes - Internal helpers
 */

/* Add a page to a working set list */
void mmap_$add_to_wsl(mmape_t *page, uint32_t vpn, uint16_t wsl_index,
                      boolean at_tail);

/* Add multiple pages to a working set list */
void mmap_$add_pages_to_wsl(uint32_t *vpn_array, uint16_t count,
                            uint16_t wsl_index);

/* Remove a page from its current working set list */
void mmap_$remove_from_wsl(mmape_t *page, uint32_t vpn);

/* Trim pages from a working set list */
void mmap_$trim_wsl(uint16_t wsl_index, uint32_t pages_to_trim);

/* Move pages to a different WSL list type */
void mmap_$move_pages_to_wsl_type(uint32_t vpn_head, uint16_t page_type,
                                  uint16_t scan_wsl_index, int16_t scan_mode);

/*
 * Function prototypes - Public API
 */

/* Set working set priority timestamp */
void MMAP_$SET_WS_PRI(void);

/* Get working set size info */
void MMAP_$GET_WS_SIZ(uint16_t wsl_index, uint32_t *page_count,
                      uint32_t *field_14, uint32_t *max_pages,
                      status_$t *status);

/* Get WSL index for a process ID */
void MMAP_$GET_WS_INDEX(uint16_t pid, uint16_t *wsl_index, status_$t *status);

/* Set maximum pages for a WSL */
void MMAP_$SET_WS_MAX(uint16_t wsl_index, uint32_t max_pages,
                      status_$t *status);

/* Free a single page */
void MMAP_$FREE(uint32_t vpn);

/* Free a list of pages */
void MMAP_$FREE_LIST(uint32_t vpn_head);

/* Free and remove a page */
void MMAP_$FREE_REMOVE(mmape_t *page, uint32_t vpn);

/* Transfer impure page to different list */
void MMAP_$IMPURE_TRANSFER(mmape_t *page, uint32_t vpn);

/* Remove unavailable page */
/*
 * MMAP_$UNAVAIL_REMOV - Remove a page from its current working set list.
 *
 * Two parameters: the VPN (long, at (0x8,A6) in the callee) and a boolean
 * that all three call sites push as TRUE (0xFF) and that the callee at
 * 0x00E0CC30 never reads.  Call sites: 0x00E13884, 0x00E13D60, 0x00E14358.
 */
void MMAP_$UNAVAIL_REMOV(uint32_t vpn, boolean unused_flag);

/* Make a page available */
void MMAP_$AVAIL(uint32_t vpn);

/* Wire a page (prevent paging) */
void MMAP_$WIRE(uint32_t vpn);

/* Unwire a page (allow paging) */
void MMAP_$UNWIRE(uint32_t vpn);

/* Install a list of pages into a WSL */
void MMAP_$INSTALL_LIST(uint32_t *vpn_array, uint16_t count, boolean use_wired);

/* Install pages for a specific process */
void MMAP_$INSTALL_PAGES(uint32_t *vpn_array, uint16_t count, uint16_t pid);

/*
 * MMAP_$FREE_PAGES - Move an array of installed pages to the free pool.
 *
 * Three arguments in the image (0x00E0CE56): (0x8,A6) a word the callee
 * never reads - ast_$flush_installed_pages pushes PROC1_$CURRENT there
 * (0x00E03FDE) - then the VPN array at (0xA,A6) and the count word at
 * (0xE,A6).  Bead source-8yhy.
 */
void MMAP_$FREE_PAGES(uint16_t pid, uint32_t *vpn_array, uint16_t count);

/* Release pages for a process */
void MMAP_$RELEASE_PAGES(uint16_t pid, uint32_t *vpn_array, uint16_t count);

/* Purge all pages from a WSL */
void MMAP_$PURGE(uint16_t wsl_index);

/* Free a process's WSL */
void MMAP_$FREE_WSL(uint16_t pid);

/* Set WSL index for a process */
void MMAP_$SET_WS_INDEX(uint16_t pid, uint16_t *wsl_index);

/* Scan working set for page replacement */
uint32_t MMAP_$WS_SCAN(uint16_t wsl_index, int16_t mode, uint32_t pages_needed,
                       uint32_t unused);

/* Get impure pages from a WSL */
void MMAP_$GET_IMPURE(uint16_t wsl_index, uint32_t *vpn_array,
                      boolean all_pages, uint16_t max_pages,
                      uint32_t *scanned, uint16_t *returned);

/* Allocate pages from a specific WSL */
void mmap_$alloc_pages_from_wsl(ws_hdr_t *wsl, uint32_t *vpn_array,
                                uint16_t count);

/* Allocate pure (code) pages */
uint16_t MMAP_$ALLOC_PURE(uint32_t *vpn_array, uint16_t count);

/* Allocate free pages */
uint16_t MMAP_$ALLOC_FREE(uint32_t *vpn_array, uint16_t count);

/* Allocate contiguous pages (stub - always fails) */
void MMAP_$ALLOC_CONTIG(uint16_t count, uint32_t *pages_alloced,
                        status_$t *status);

/* Reclaim pages into a WSL */
void MMAP_$RECLAIM(uint32_t *vpn_array, uint16_t count, boolean use_wired);

/* Initialize MMAP subsystem */
void MMAP_$INIT(void *param);

/* Get remote pool (stub - returns input) */
uint32_t MMAP_$REMOTE_POOL(uint32_t param);

/*
 * GET_WIRED - Get wired memory buffer
 *
 * Returns a pointer to a pre-allocated wired (non-pageable) memory buffer
 * at address 0xE2E07C. This is used for kernel data structures that must
 * always be resident in physical memory.
 *
 * Original address: 0x00e1d8dc
 */
void *GET_WIRED(void);

#endif /* MMAP_H */
