/*
 * MMAP Internal Header
 *
 * Internal data structures and globals for the MMAP subsystem.
 * This header should only be included by mmap/ source files.
 */

#ifndef MMAP_INTERNAL_H
#define MMAP_INTERNAL_H

#include "mmap.h"

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
 */
extern void *SEGMENT_TABLE[];

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

/*
 * ============================================================================
 * Error Status Arrays (internal)
 * ============================================================================
 */

extern status_$t Illegal_WSL_Index_Err[];
extern status_$t Illegal_PID_Err[];
extern status_$t WSL_Exhausted_Err[];
extern status_$t MMAP_Bad_Unavail_err[];
extern status_$t mmap_bad_avail[];
extern status_$t MMAP_Bad_Reclaim_Err[];
extern status_$t Inconsistent_MMAPE_Err[];
extern status_$t MMAP_Error_Examined_Max[];

#endif /* MMAP_INTERNAL_H */
