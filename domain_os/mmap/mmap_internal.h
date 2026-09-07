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
 * Global lock for MMAP data structures
 * Located at MMAP_GLOBALS base.
 */
extern void *MMAP_LOCK;

/*
 * Working set owner tracking
 * Used during WSL allocation.
 */
extern uint16_t MMAP_$WS_OWNER;

/*
 * Memory examination table (max 3 ranges)
 * Tracks physical memory ranges found during init.
 * Located at 0xE007EC (m68k).
 */
extern mem_range_t MEM_EXAM_TABLE[];

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
 * Internal statistics counters
 * Located in MMAP global data area.
 */
extern uint32_t MMAP_$WSL_DIRTY_RMT_CNT;    /* 0xE23344: MMAP_$WSL[4].page_count */
extern uint32_t MMAP_$WSL_DIRTY_LOCAL_CNT;  /* 0xE23320: MMAP_$WSL[3].page_count */

/*
 * Working set list index table - maps ASID to WSL index
 */
extern uint16_t MMAP_$WSL_INDEX_TABLE[];

/*
 * Working set limit data - per-WSL limits and parameters
 */
extern uint32_t MMAP_$WS_LIMIT_DATA[];

/*
 * Working set data array
 */
extern uint32_t MMAP_$WS_DATA[];

/*
 * Process working set list array
 */
extern uint16_t MMAP_$PROC_WS_LIST[];

#endif /* MMAP_INTERNAL_H */
