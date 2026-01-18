/*
 * PCHIST_$ - Internal header for PC histogram subsystem
 *
 * Contains internal helper function prototypes and data structure
 * details not needed by external callers.
 */

#ifndef PCHIST_INTERNAL_H
#define PCHIST_INTERNAL_H

#include "pchist/pchist.h"
#include "fim/fim.h"
#include "proc1/proc1.h"
#include "proc2/proc2.h"
#include "term/term.h"

/*
 * Internal data offsets from PCHIST_$CONTROL base (0xe2c204)
 * These are used for bitmap and per-process PC storage
 */
#define PCHIST_BITMAP_OFFSET        0x18    /* Process enable bitmap */
#define PCHIST_PROC_PC_OFFSET       0x1C    /* Per-process last PC */
#define PCHIST_SYS_COUNT_OFFSET     0x120   /* System profiling count */
#define PCHIST_PROC_COUNT_OFFSET    0x122   /* Process profiling count */
#define PCHIST_ENABLED_OFFSET       0x124   /* Histogram enabled flag */
#define PCHIST_DOALIGN_OFFSET       0x126   /* Alignment flag */

/*
 * Wire area data for histogram buffer
 * Located at 0xe85c14 (array of page pointers)
 */
extern uint32_t PCHIST_$WIRE_PAGES[];

/*
 * Count of wired pages
 * Located at 0xe8604e
 */
extern int16_t PCHIST_$WIRED_COUNT;

/*
 * MST wire area context
 * Located at 0xe85c18
 */
extern void *PCHIST_$WIRE_CONTEXT;

/*
 * ============================================================================
 * Internal Helper Functions
 * ============================================================================
 */

/*
 * PCHIST_$ENABLE_TERMINAL - Enable/disable terminal profiling display
 *
 * Calls TERM_$PCHIST_ENABLE with the total profiling count and
 * optionally writes a message when profiling starts.
 *
 * Parameters:
 *   disabling - 0 if enabling, non-zero if disabling
 *
 * Original address: 0x00e5ca00
 */
void PCHIST_$ENABLE_TERMINAL(int16_t disabling);

/*
 * PCHIST_$UNWIRE_CLEANUP - Clean up wired pages and disable profiling
 *
 * Called when profiling is stopped to unwire all pages that were
 * wired for the histogram buffer.
 *
 * Original address: 0x00e5cd02
 */
void PCHIST_$UNWIRE_CLEANUP(void);

/*
 * PCHIST_$STOP_PROFILING - Stop system-wide profiling
 *
 * Decrements the system profiling count and cleans up if
 * count reaches zero.
 *
 * Original address: 0x00e5cd66
 */
void PCHIST_$STOP_PROFILING(void);

/*
 * ============================================================================
 * Macros for bitmap manipulation
 * ============================================================================
 * The process profiling bitmap uses big-endian bit ordering where
 * process N's bit is at byte (N-1)/8, bit 7-((N-1)&7)
 */

/*
 * Test if process pid has profiling enabled
 * pid is 1-based
 */
#define PCHIST_PROC_ENABLED(bitmap, pid) \
    (((bitmap)[((pid) - 1) >> 3] & (0x80 >> (((pid) - 1) & 7))) != 0)

/*
 * Set profiling enabled for process pid
 */
#define PCHIST_PROC_SET(bitmap, pid) \
    ((bitmap)[((pid) - 1) >> 3] |= (0x80 >> (((pid) - 1) & 7)))

/*
 * Clear profiling for process pid
 */
#define PCHIST_PROC_CLEAR(bitmap, pid) \
    ((bitmap)[((pid) - 1) >> 3] &= ~(0x80 >> (((pid) - 1) & 7)))

#endif /* PCHIST_INTERNAL_H */
