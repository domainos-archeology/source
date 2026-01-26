/*
 * mst_data.c - MST Module Global Data Definitions
 *
 * This file defines the global variables used by the MST (Memory Segment
 * Table) module for address space and segment management.
 *
 * Original M68K addresses:
 *   MST_$ASID_LIST:              0xE24384 (8 bytes)   - ASID bitmap
 *   MST_$MAP_ALTER_LOCK:         0xE2438C (24 bytes)  - Map alter exclusion lock
 *   MST_$ASID_ALLOCATE_LOCK:     0xE243A4 (24 bytes)  - ASID allocation exclusion lock
 *   MST_$ASID_LOCK:              0xE243BC (24 bytes)  - ASID exclusion lock
 *   MST_$TOUCH_COUNT:            0xE24448 (2 bytes)   - Touch-ahead count
 *   MST_$GLOBAL_B_SIZE:          0xE2444A (2 bytes)   - Global B size (0x60)
 *   MST_$SEG_MEM_TOP:            0xE2444C (2 bytes)   - Top of memory (0x200)
 *   MST_$SEG_HIGH:               0xE2444E (2 bytes)   - Highest segment (0x1F8)
 *   MST_$SEG_GLOBAL_B_OFFSET:    0xE24450 (2 bytes)   - Global B offset (0x140)
 *   MST_$SEG_GLOBAL_B:           0xE24452 (2 bytes)   - First global B (0x1A0)
 *   MST_$SEG_PRIVATE_B_OFFSET:   0xE24454 (2 bytes)   - Private B offset (0x60)
 *   MST_$SEG_PRIVATE_B_END:      0xE24456 (2 bytes)   - Last private B (0x19F)
 *   MST_$SEG_PRIVATE_B:          0xE24458 (2 bytes)   - First private B (0x198)
 *   MST_$SEG_PRIVATE_A_END:      0xE2445A (2 bytes)   - Last private A (0x137)
 *   MST_$PRIVATE_A_SIZE:         0xE2445C (2 bytes)   - Private A size (0x138)
 *   MST_$SEG_GLOBAL_A_END:       0xE2445E (2 bytes)   - Last global A (0x197)
 *   MST_$SEG_GLOBAL_A:           0xE24460 (2 bytes)   - First global A (0x138)
 *   MST_$GLOBAL_A_SIZE:          0xE24462 (2 bytes)   - Global A size (0x60)
 *   MST_$SEG_TN:                 0xE24464 (2 bytes)   - Total segments (0x140)
 *   MST_$GOT_COLOR:              0xE24466 (2 bytes)   - Color support flag
 */

#include "mst/mst.h"

/*
 * ============================================================================
 * ASID Management
 * ============================================================================
 */

/*
 * ASID bitmap
 *
 * Tracks which ASIDs are allocated. Bit set = ASID is in use.
 * Supports up to 64 ASIDs (0-63), though only 58 are typically used.
 *
 * Original address: 0xE24384
 */
uint8_t MST_$ASID_LIST[8] = { 0 };

/*
 * Per-ASID base table
 *
 * Maps each ASID to its starting index in the MST page table array.
 *
 * Original address: 0xE243D4 (calculated based on structure layout)
 */
uint16_t MST_ASID_BASE[MST_MAX_ASIDS] = { 0 };

/*
 * ============================================================================
 * Exclusion Locks
 * ============================================================================
 */

/*
 * Map alteration exclusion lock
 *
 * Protects map modification operations.
 *
 * Original address: 0xE2438C
 */
ml_$exclusion_t MST_$MAP_ALTER_LOCK = { 0 };

/*
 * ASID allocation exclusion lock
 *
 * Protects ASID allocation/deallocation.
 *
 * Original address: 0xE243A4
 */
ml_$exclusion_t MST_$ASID_ALLOCATE_LOCK = { 0 };

/*
 * ASID operations exclusion lock
 *
 * Protects general ASID operations.
 *
 * Original address: 0xE243BC
 */
ml_$exclusion_t MST_$ASID_LOCK = { 0 };

/*
 * ============================================================================
 * Segment Configuration
 * ============================================================================
 *
 * These values define the virtual address space layout.
 * On M68020, the layout is:
 *   0x000-0x137: Private A (process-local, 312 segments)
 *   0x138-0x197: Global A (shared, 96 segments)
 *   0x198-0x19F: Private B (process-local, 8 segments)
 *   0x1A0-0x1FF: Global B (shared, 96 segments)
 *
 * Each segment covers 32KB (0x8000 bytes).
 */

/*
 * Touch-ahead page count
 *
 * Number of pages to prefetch when touching memory.
 *
 * Original address: 0xE24448
 */
uint16_t MST_$TOUCH_COUNT = 0;

/*
 * Global B region size (number of segments)
 *
 * Original address: 0xE2444A
 */
uint16_t MST_$GLOBAL_B_SIZE = 0x60;

/*
 * Top of addressable memory (segment number)
 *
 * Original address: 0xE2444C
 */
uint16_t MST_$SEG_MEM_TOP = 0x200;

/*
 * Highest valid segment number
 *
 * Original address: 0xE2444E
 */
uint16_t MST_$SEG_HIGH = 0x1F8;

/*
 * Global B offset in tables
 *
 * Offset to add when accessing global B entries.
 *
 * Original address: 0xE24450
 */
uint16_t MST_$SEG_GLOBAL_B_OFFSET = 0x140;

/*
 * First segment in global B region
 *
 * Original address: 0xE24452
 */
uint16_t MST_$SEG_GLOBAL_B = 0x1A0;

/*
 * Private B offset in tables
 *
 * Offset to add when accessing private B entries.
 *
 * Original address: 0xE24454
 */
uint16_t MST_$SEG_PRIVATE_B_OFFSET = 0x60;

/*
 * Last segment in private B region
 *
 * Original address: 0xE24456
 */
uint16_t MST_$SEG_PRIVATE_B_END = 0x19F;

/*
 * First segment in private B region
 *
 * Original address: 0xE24458
 */
uint16_t MST_$SEG_PRIVATE_B = 0x198;

/*
 * Last segment in private A region
 *
 * Original address: 0xE2445A
 */
uint16_t MST_$SEG_PRIVATE_A_END = 0x137;

/*
 * Private A region size (number of segments)
 *
 * Original address: 0xE2445C
 */
uint16_t MST_$PRIVATE_A_SIZE = 0x138;

/*
 * Last segment in global A region
 *
 * Original address: 0xE2445E
 */
uint16_t MST_$SEG_GLOBAL_A_END = 0x197;

/*
 * First segment in global A region
 *
 * Original address: 0xE24460
 */
uint16_t MST_$SEG_GLOBAL_A = 0x138;

/*
 * Global A region size (number of segments)
 *
 * Original address: 0xE24462
 */
uint16_t MST_$GLOBAL_A_SIZE = 0x60;

/*
 * Total number of segments in MST
 *
 * Original address: 0xE24464
 */
uint16_t MST_$SEG_TN = 0x140;

/*
 * Color display support flag
 *
 * Non-zero if system has color display hardware.
 *
 * Original address: 0xE24466
 */
uint16_t MST_$GOT_COLOR = 0;

/*
 * ============================================================================
 * Wiring State
 * ============================================================================
 */

/*
 * Number of wired MST pages
 *
 * Count of MST pages currently wired in memory.
 */
uint16_t MST_$MST_PAGES_WIRED = 0;

/*
 * Maximum MST pages to wire
 *
 * Limit on how many MST pages can be wired.
 */
uint16_t MST_$MST_PAGES_LIMIT = 0;
