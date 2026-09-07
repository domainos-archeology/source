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
 * Segment info table
 * Array of pointers to segment descriptors.
 * Located at 0xEC5400 (m68k).
 *
 * NOTE: this `void *` view does not match the machine code.  The table is
 * an array of 0x14-byte aste_t records addressed 1-based:
 * mmap_$trim_wsl (0x00E0C8C0) and AREA_$DEACTIVATE_ASTE (0x00E0A096) both
 * form `0xEC5400 + seg * 0x14` and then use a negative displacement into
 * the PREVIOUS record.  Use MMAP_$SEG_ASTE below for new code; the four
 * remaining `SEGMENT_TABLE[seg]` users are tracked by bead source-4in.
 */
extern void *SEGMENT_TABLE[];

/*
 * The same storage at 0xEC5400, correctly typed.
 *
 *   00e0c8c0  movea.l #0xec5400,A1
 *   00e0c8c6  lsl.w #0x2,D0w / lsl.w #0x2,D1w / add.w D1w,D0w   ; seg * 0x14
 *   00e0c8ce  lea (0x0,A1,D0w),A1
 *   00e0c8d2  movea.l (-0x10,A1),A3          ; record(seg)+0x04 = aste->aote
 *
 * so record(seg) = 0xEC5400 + (seg - 1) * sizeof(aste_t).
 */
extern aste_t MMAP_$SEG_ASTE[];
#define MMAP_$SEG_ASTE_FOR(seg) (&MMAP_$SEG_ASTE[(seg) - 1])

/*
 * Internal statistics counters
 * Located in MMAP global data area.
 */
extern uint32_t DAT_00e23344;  /* Page count statistic */
extern uint32_t DAT_00e23320;  /* Page count statistic */

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
