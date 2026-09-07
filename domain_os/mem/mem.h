/*
 * MEM - Memory Management Support Module
 *
 * This module provides low-level memory management support functions
 * for Domain/OS.  In the SAU2 image it is a very small module: the map
 * has exactly one code segment holding one routine
 *
 *     I  E0ADB0  MEM_   size = B8
 *        E0ADB0  MEM_$PARITY_LOG
 *
 * and one data segment holding the module's whole A5 block
 *
 *     D  E22930  MEM_   size = 5C
 *        E22930  MEM_$SIZE
 *        E22934  MEM_$MEM_REC
 *
 * (the next data segment, PARITY, starts at 0xE2298C).
 *
 * This module is distinct from:
 * - MMU: Hardware MMU interface
 * - MMAP: Memory map management
 * - AST: Active Segment Table management
 *
 * Original source was likely Pascal, converted to C.
 */

#ifndef MEM_H
#define MEM_H

#include "base/base.h"
#include "mmap/mmap.h"   /* MMAP_$REAL_PAGES */

/*
 * ============================================================================
 * mem_$page_error_t - one per-page parity-error record (18 bytes)
 * ============================================================================
 *
 * MEM_$PARITY_LOG walks these with `lea (0x12,A5),A0` / `lea (0x12,A0),A0`,
 * which pins both the base (module offset 0x12 = 0xE22942) and the 0x12-byte
 * stride.  Inside a record the routine touches exactly two fields:
 *
 *   00e0ade8  tst.w (0x4,A0)      -> count, a word at +0x04
 *   00e0adf0  and.b (0x1,A0),D3b  -> byte +0x01 of the longword at +0x00,
 *                                    i.e. bits 23..16 of the physical address
 *   00e0ae56  move.l (0x8,A6),(0x0,A5,D0)  -> phys_addr, a longword at +0x00
 *
 * The remaining 12 bytes are never read or written by the SAU2 image; they
 * are only carried along by the bulk copy in ASKNODE_$INTERNET_INFO.
 *
 * PACKED: m68k aligns 32-bit fields to 2 bytes, so the recovered offsets are
 * only reproducible on a 4/8-byte-aligning host if the record is packed.
 * Packing changes no m68k layout.
 */
typedef struct mem_$page_error_t {
  uint32_t phys_addr;   /* 0x00: physical address of the failing page */
  uint16_t count;       /* 0x04: number of errors charged to this record */
  uint8_t  reserved[12];/* 0x06: never touched by the image */
} __attribute__((packed)) mem_$page_error_t;

_Static_assert(__builtin_offsetof(mem_$page_error_t, phys_addr) == 0x00, "mem_$page_error_t.phys_addr");
_Static_assert(__builtin_offsetof(mem_$page_error_t, count) == 0x04, "mem_$page_error_t.count");
_Static_assert(__builtin_offsetof(mem_$page_error_t, reserved) == 0x06, "mem_$page_error_t.reserved");
_Static_assert(sizeof(mem_$page_error_t) == 0x12, "mem_$page_error_t is an 18-byte record");

/* Number of per-page error records in MEM_$MEM_REC (0x12 + 4*0x12 = 0x5A,
 * exactly the end of the 0x56-byte block ASKNODE_$INTERNET_INFO copies). */
#define MEM_PAGE_ERROR_RECORDS 4

/*
 * ============================================================================
 * mem_$mem_rec_t - MEM_$MEM_REC, the 0x56-byte memory statistics record
 * ============================================================================
 *
 * ASKNODE_$INTERNET_INFO copies this record verbatim into its reply:
 *
 *   00e64812  movea.l #0xe22934,A0
 *   00e6481c  moveq #0x14,D3
 *   00e6481e  lea (0x72,A1),A3
 *   00e64824  move.l (A0)+,(A3)+
 *   00e64826  dbf D3w,0x00e64824      ; 21 longwords
 *   00e6482a  move.w (A0)+,(A3)+      ; + 1 word  = 0x56 bytes
 *
 * so the record runs 0xE22934..0xE22989 and the page-error array is its tail.
 * Offsets below are relative to MEM_$MEM_REC (module offset 0x04).
 */
/* Only mem_$page_error_t needs packing; these two records are all-word (and
 * word-aligned) members, so a 2-byte-aligning m68k and a 4/8-byte-aligning
 * host lay them out identically.  Leaving them unpacked keeps &MEM_$MEM_REC a
 * normally-aligned pointer for ASKNODE_$INTERNET_INFO's bulk copy. */
typedef struct mem_$mem_rec_t {
  /* 0x00 (0xE22934) and 0x02 (0xE22936): two words that the image ships
   * pre-set to 0x0002 (`gsk read 0x00E22934 4` -> 00 02 00 02) and that
   * nothing in the SAU2 image ever reads or writes -
   * they are copied out by ASKNODE_$INTERNET_INFO and nothing else.
   * TODO(source-xx4l, 0xE22934): purpose unrecovered; a later SAU with a
   * MEM_$INIT (sr10.4 sau11/sau14) may name them. */
  uint16_t w_00;
  uint16_t w_02;
  /*
   * 0x04 (0xE22938): per-board parity error counts, indexed 1..2.
   *
   *   00e0adce  move.w D0w,D1w     ; D0 = 1 (< 3MB) or 2 (>= 3MB)
   *   00e0add0  ext.l D1
   *   00e0add2  add.l D1,D1        ; D1 = 2*board
   *   00e0add4  addq.w #0x1,(0x8,A5,D1*0x1)
   *
   * so board 1 is counted at module offset 0x0A (0xE2293A) and board 2 at
   * 0x0C (0xE2293C); the word at 0x08 that index 0 would name is never
   * touched (it is the array's Pascal 1-based bias slot).
   */
  uint16_t board_errors[3];
  uint16_t w_0a;        /* 0x0A (0xE2293E): unreferenced */
  uint16_t w_0c;        /* 0x0C (0xE22940): unreferenced */
  /* 0x0E (0xE22942): the per-page records, MEM_$PARITY_LOG's table */
  mem_$page_error_t page_errors[MEM_PAGE_ERROR_RECORDS];
} mem_$mem_rec_t;

_Static_assert(__builtin_offsetof(mem_$mem_rec_t, w_00) == 0x00, "mem_$mem_rec_t.w_00");
_Static_assert(__builtin_offsetof(mem_$mem_rec_t, w_02) == 0x02, "mem_$mem_rec_t.w_02");
_Static_assert(__builtin_offsetof(mem_$mem_rec_t, board_errors) == 0x04, "mem_$mem_rec_t.board_errors");
_Static_assert(__builtin_offsetof(mem_$mem_rec_t, w_0a) == 0x0A, "mem_$mem_rec_t.w_0a");
_Static_assert(__builtin_offsetof(mem_$mem_rec_t, w_0c) == 0x0C, "mem_$mem_rec_t.w_0c");
_Static_assert(__builtin_offsetof(mem_$mem_rec_t, page_errors) == 0x0E, "mem_$mem_rec_t.page_errors");
_Static_assert(sizeof(mem_$mem_rec_t) == 0x56,
               "mem_$mem_rec_t: the 0x56 bytes ASKNODE_$INTERNET_INFO copies from 0xE22934");

/*
 * ============================================================================
 * MEM_DATA - the MEM module's A5 block at 0x00E22930 (map size 0x5C)
 * ============================================================================
 *
 * MEM_$PARITY_LOG establishes it with `lea (0xe22930).l,A5` at 0x00E0ADB8 and
 * addresses every cell it touches off A5, so MEM_$SIZE, MEM_$MEM_REC and the
 * parity tables are fields of one object rather than separate globals.
 */
typedef struct mem_data_t {
  uint32_t       size;  /* 0x00 (0xE22930) MEM_$SIZE */
  mem_$mem_rec_t rec;   /* 0x04 (0xE22934) MEM_$MEM_REC */
  uint16_t       w_5a;  /* 0x5A (0xE2298A) tail of the 0x5C segment;
                         * outside the record ASKNODE copies, unreferenced */
} mem_data_t;

_Static_assert(__builtin_offsetof(mem_data_t, size) == 0x00, "mem_data_t.size");
_Static_assert(__builtin_offsetof(mem_data_t, rec) == 0x04, "mem_data_t.rec");
_Static_assert(__builtin_offsetof(mem_data_t, w_5a) == 0x5A, "mem_data_t.w_5a");
_Static_assert(sizeof(mem_data_t) == 0x5C,
               "mem_data_t: map segment MEM_ 0x00E22930 size = 5C");

extern mem_data_t MEM_DATA;

/*
 * The two names the SAU2 map exports, plus the two tables the tree refers to
 * by their Ghidra labels.  These are field aliases, not address arithmetic,
 * so the m68k and host builds see identical offsets.
 */
#define MEM_$SIZE         (MEM_DATA.size)                /* 0xE22930 */
#define MEM_$MEM_REC      (MEM_DATA.rec)                 /* 0xE22934 */
#define MEM_$BOARD_ERRORS (MEM_DATA.rec.board_errors)    /* 0xE22938, 1-based */
#define MEM_$PAGE_ERRORS  (MEM_DATA.rec.page_errors)     /* 0xE22942 */

/*
 * MEM_$PARITY_LOG - Log a memory parity error
 *
 * Records a parity error in MEM_$MEM_REC.  The record tracks:
 *   - a total error count per memory board (board 1 < 3MB, board 2 >= 3MB)
 *   - a per-page error count for the four most frequently failing pages
 *
 * When a new page has an error and the per-page table is full, the entry with
 * the lowest count is replaced.
 *
 * Parameters:
 *   phys_addr - Physical address where the parity error occurred
 *
 * Original address: 0x00E0ADB0
 * Size: 182 bytes
 */
void MEM_$PARITY_LOG(uint32_t phys_addr);

#endif /* MEM_H */
