/*
 * MMAP Internal Header
 *
 * Internal data structures and globals for the MMAP subsystem.
 * This header should only be included by mmap/ source files.
 */

#ifndef MMAP_INTERNAL_H
#define MMAP_INTERNAL_H

#include "mmap/mmap.h"
#include "dump/dump.h"  /* mem_range_t, DUMP_$ADDRS (MMAP_$INIT fills it) */
#include "mmu/mmu.h"  /* For PMAPE_FOR_VPN, PMAPE_FLAG_* */
#include "ast/ast.h" /* For aste_t / aote_t (the 0xEC5400 segment table) */
#include "pmap/pmap.h" /* PMAP_SEGMAP, read by mmap_$trim_wsl */

/*
 * ============================================================================
 * Internal Type Definitions
 * ============================================================================
 */

/* mem_range_t, the element type of DUMP_$ADDRS, moved to dump/dump.h with
 * the array it describes (bead source-3uo). */

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

/* DUMP_$ADDRS / DUMP_ADDRS_RANGES: see dump/dump.h (bead source-3uo). */

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

/*
 * ============================================================================
 * Constant cells in the MMAP_ code segment
 * ============================================================================
 *
 * The compiler places the status longwords handed to CRASH_SYSTEM in the
 * code region and passes them with `pea (d,PC)`.  A cell reached from more
 * than one routine is one object, defined in mmap/mmap_data.c.
 */

/*
 * 0x00E0CCB8: 00 06 00 04 = status_$mmap_bad_avail ("bad avail" in the
 * SR10.2 status-code database).  Passed by MMAP_$AVAIL (`pea (0x2a,PC)` at
 * 0x00E0CC8C) and MMAP_$UNWIRE (`pea (-0x8c,PC)` at 0x00E0CD42).
 */
extern const status_$t mmap_$bad_avail_00e0ccb8;

/*
 * 0x00E0D1C4: 00 06 00 0a = status_$mmap_illegal_pid ("illegal pid").
 * Passed by MMAP_$FREE_WSL (`pea (0x52,PC)` at 0x00E0D170) and
 * MMAP_$SET_WS_INDEX (0x00E0D1E4).
 */
extern const status_$t mmap_$illegal_pid_00e0d1c4;

/*
 * 0x00E0C9E0: 00 06 00 09 = status_$mmap_illegal_wsl_index ("illegal wsl
 * index").  Passed by MMAP_$SET_WS_PRI, MMAP_$PURGE (`pea (-0x756,PC)` at
 * 0x00E0D134), MMAP_$SET_WS_INDEX (0x00E0D22E) and MMAP_$WS_SCAN.
 */
extern const status_$t mmap_$illegal_wsl_index_00e0c9e0;

/*
 * The pages_to_trim value MMAP_$PURGE hands mmap_$trim_wsl (`move.l
 * #0x3fffff,-(SP)` at 0x00E0D142); mmap_$trim_wsl recognises it with
 * `cmpi.l #0x3fffff,D6 / seq` (0x00E0C770) and purges the whole list.
 */
#define MMAP_TRIM_PURGE_ALL 0x3FFFFF

#endif /* MMAP_INTERNAL_H */
