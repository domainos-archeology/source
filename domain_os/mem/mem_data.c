/*
 * mem_data.c - MEM Module Global Data Definitions
 *
 * This file defines the global variables used by the MEM (Memory Support)
 * module. These primarily relate to memory error tracking.
 *
 * Original M68K addresses:
 *   MEM_$SIZE:         0xE22930 (4 bytes)  - Total system memory size
 *   MEM_$MEM_REC:      0xE22934 (4 bytes)  - Memory record base
 *   MEM_$BOARD_ERRORS: 0xE22938 (4 bytes)  - Per-board error counts (2 x 2 bytes)
 *   MEM_$PAGE_ERRORS:  0xE22942 (72 bytes) - Per-page error tracking (4 x 18 bytes)
 *
 * Memory layout:
 *   0xE22930: MEM_$SIZE (4 bytes)
 *   0xE22934: MEM_$MEM_REC (4 bytes)
 *   0xE22938: Board 1 error count (2 bytes) - for addresses < 3MB
 *   0xE2293A: Board 2 error count (2 bytes) - for addresses >= 3MB
 *   0xE2293C: Reserved (6 bytes)
 *   0xE22942: Page error entries (4 entries x 18 bytes each)
 *
 * Each page error entry (18 bytes):
 *   Offset 0x00: Physical address (4 bytes)
 *   Offset 0x04: Error count (2 bytes)
 *   Offset 0x06: Reserved (12 bytes)
 */

#include "mem/mem.h"

/*
 * ============================================================================
 * Memory Configuration
 * ============================================================================
 */

/*
 * Total system memory size in bytes
 *
 * Original address: 0xE22930
 */
uint32_t MEM_$SIZE = 0;

/*
 * Memory record base
 *
 * Used as base address for memory tracking structures.
 *
 * Original address: 0xE22934
 */
uint32_t MEM_$MEM_REC = 0;

/*
 * ============================================================================
 * Parity Error Tracking
 * ============================================================================
 */

/*
 * Per-board error counts
 *
 * Array of error counts per memory board:
 *   [0]: Board 1 errors (addresses < 3MB)
 *   [1]: Board 2 errors (addresses >= 3MB)
 *
 * Original address: 0xE22938
 */
uint16_t MEM_$BOARD_ERRORS[2] = { 0 };

/*
 * Per-page error tracking table
 *
 * Tracks the most frequently failing pages. When a new page has an error
 * and the table is full, the entry with the lowest count is replaced.
 *
 * Each entry is 18 bytes:
 *   - 4 bytes: Physical address
 *   - 2 bytes: Error count
 *   - 12 bytes: Reserved/padding
 *
 * Original address: 0xE22942
 */
typedef struct {
    uint32_t    phys_addr;      /* Physical address of failing page */
    uint16_t    error_count;    /* Number of errors at this address */
    uint8_t     reserved[12];   /* Padding to 18 bytes */
} mem_$page_error_t;

mem_$page_error_t MEM_$PAGE_ERRORS[4] = { { 0 } };
