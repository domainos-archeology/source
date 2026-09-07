/*
 * MMAP Internal Header
 *
 * Internal data structures and globals for the MMAP subsystem.
 * This header should only be included by mmap/ source files.
 */

#ifndef MMAP_INTERNAL_H
#define MMAP_INTERNAL_H

#include "mmap/mmap.h"
#include "mmu/mmu.h"  /* For PMAPE_FOR_VPN, PMAPE_FLAG_* */
#include "ast/ast.h" /* For aste_t / aote_t (the 0xEC5400 segment table) */

/*
 * ============================================================================
 * Internal Type Definitions
 * ============================================================================
 */

/*
 * Memory range descriptor
 * Used for tracking physical memory ranges during initialization.
 */
typedef struct mem_range_t {
    uint32_t start;
    uint32_t end;
} mem_range_t;

/*
 * ============================================================================
 * Internal Global Data
 * ============================================================================
 */

/*
 * The MMAP_ module data block is one object, mmap_globals_t (mmap/mmap.h).
 * Its spin lock is MMAP_GLOBALS.lock at offset 0 - the cell every entry
 * point hands ML_$SPIN_LOCK as the bare A5 base - and every separately named
 * cell of the block, MMAP_$WS_OWNER included, is an accessor macro over the
 * same object declared there.
 */

/*
 * DUMP_$ADDRS - the physical memory ranges MMAP_$INIT hands to the crash-dump
 * code.  Named by the SAU2 map (`E007EC DUMP_$ADDRS`, inside the DUMP
 * segment, 0x14 bytes up to the APP segment at 0xE00800); the tree used to
 * call it MEM_EXAM_TABLE.
 *
 * MMAP_$INIT walks it with `movea.l #0xe007ec,A4 / lea (A4),A3` and
 * `addq.l #0x8,A3` per range, writing start at (-0x8,A3) and end at
 * (-0x4,A3) (0x00E319D6-0x00E31A2E), and crashes once the range count passes
 * 2 (`cmpi.w #0x2,D4w / ble`, 0x00E31A0C) - so two 8-byte ranges are all it
 * will fill.  DUMP reads range 0's end at 0x00E004E8 and range 1's start at
 * 0x00E004E4.
 *
 * Image contents: range 0 = { 0x00100000, 0x0017FC00 }, range 1 = { 0, 0 }.
 * The last 4 bytes of the map's 0x14 extent (0xE007FC) have no reference.
 */
#define DUMP_ADDRS_RANGES 2
extern mem_range_t DUMP_$ADDRS[DUMP_ADDRS_RANGES];

/*
 * Segment info table, at 0xEC5400 (m68k).
 *
 *   00e0c8c0  movea.l #0xec5400,A1
 *   00e0c8c6  lsl.w #0x2,D0w / lsl.w #0x2,D1w / add.w D1w,D0w   ; seg * 0x14
 *   00e0c8ce  lea (0x0,A1,D0w),A1
 *   00e0c8d2  movea.l (-0x10,A1),A3          ; record(seg)+0x04 = aste->aote
 *
 * so record(seg) = 0xEC5400 + (seg - 1) * sizeof(aste_t).
 *
 * Every user of the table goes through these two names; the old
 * `void *SEGMENT_TABLE[]` view, which indexed the same storage with a
 * 4-byte stride, is gone (bead source-uxu3).
 */
extern aste_t MMAP_$SEG_ASTE[];
#define MMAP_$SEG_ASTE_FOR(seg) (&MMAP_$SEG_ASTE[(seg) - 1])

/*
 * The 0x14-byte stride is the whole basis of the addressing above: the
 * `lsl.w #0x2 / lsl.w #0x2 / add.w` sequence multiplies the segment number
 * by exactly sizeof(aste_t).
 */
#if defined(ARCH_M68K)
_Static_assert(sizeof(aste_t) == 0x14, "MMAP_$SEG_ASTE stride must be 0x14");
#endif

/*
 * The 16-bit attribute-flags word at aote+0x0E, which aote_t splits into
 * two bytes.  Read it as a word so the code does not depend on the host
 * byte order.
 */
#define MMAP_AOTE_ATTR_FLAGS(aote)                                             \
    ((uint16_t)(((uint16_t)(aote)->attr_flags_hi << 8) | (aote)->attr_flags_lo))

/*
 * Bit 12 of that word (bit 4 of attr_flags_hi) -- the bit
 * AST_$SET_ATTR_DISPATCH writes for attribute type 0 and for a non-zero
 * reference count (0xE04CF2, 0xE04D0C).  MMAP_$WS_SCAN (0x00E0D442) and
 * MMAP_$GET_IMPURE (0x00E0D686) both `btst.l #0xc` it.
 */
#define MMAP_AOTE_ATTR_FLAG_BIT12 0x1000


/*
 * MMAP_$WSL_INDEX_TABLE, MMAP_$WS_LIMIT_DATA, MMAP_$WS_DATA and
 * MMAP_$PROC_WS_LIST were tree-invented views with no cell of their own.
 * OSINFO_$GET_MMAP's disassembly shows what each of them really is:
 *
 *   MMAP_$WSL_INDEX_TABLE[asid - 1]  -> MMAP_$WS_OWNER[asid - 1]  (0xE23CA8,
 *                                       0x00E5C71C)
 *   MMAP_$WS_LIMIT_DATA[i * 9 + 8]   -> MMAP_WSL[i].ws_floor      (0xE232B0,
 *                                       0x00E5C762-0x00E5C772)
 *   MMAP_$WS_DATA[i * 9]             -> MMAP_WSL[i].page_count    (0xE232B4,
 *                                       0x00E5C898-0x00E5C8B8)
 *   MMAP_$PROC_WS_LIST[i]            -> PROC1_$TYPE[i]            (0xE2612C,
 *                                       0x00E5C97C-0x00E5C99E)
 *
 * so the four declarations are gone and their users name the real objects.
 */

#endif /* MMAP_INTERNAL_H */
