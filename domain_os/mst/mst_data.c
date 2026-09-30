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
 *
 * The MST_UNWIRED data segment (SAU2 map: E7CF0C, size = 0x48) holds:
 *   MST_$PAGE_AVAIL_BITMAP:      0xE7CF0C (0x30 bytes) - 12 longwords
 *   MST_$PAGE_ALLOC_HINT:        0xE7CF3C (2 bytes)   - bitmap search hint
 *   MST_$MST_PAGES_LIMIT:        0xE7CF3E (2 bytes)   - map symbol
 *   MST_$MST_PAGES_WIRED:        0xE7CF40 (2 bytes)   - map symbol
 *   (unnamed word table)         0xE7CF42 (0x12 bytes) - see TODO below
 *
 * And the segment table itself lives in the uninitialised VM_TABLES area:
 *   MST:                         0xEE5800 (0xC00 bytes) - 0x600 words
 */

#include "mst/mst_internal.h"

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
 * One Domain Pascal SET of eight bytes.  ASID N is bit N & 7 of byte
 * (63 - N) >> 3 (MST_$ALLOC_ASID, 0x00E42D3A), i.e. the set reads as a
 * big-endian 64-bit integer with bit N = 1 << N.
 *
 * MST_$INIT reaches the same storage as two longwords, at 0xE24384 (the cell
 * the link inventory calls MST_$ASID_LIST_LONG) and 0xE24388.  Those are not
 * separate objects - the SAU2 map has MST_$ASID_LIST at E24384 and the next
 * symbol, MST_$MAP_ALTER_LOCK, at E2438C - so they are written through
 * MST_$ASID_LIST_STORE_LONG() in mst/mst_internal.h rather than defined here.
 *
 * Image bytes at 0xE24384: 00 00 00 00 00 00 00 00.
 *
 * Original address: 0xE24384 (8 bytes)
 */
uint8_t MST_$ASID_LIST[8] = { 0 };
_Static_assert(sizeof(MST_$ASID_LIST) == 8, "MST_$ASID_LIST is E24384..E2438B");

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
 * A Domain boolean: MST_$DISKLESS_INIT stores its byte argument here with
 * `move.b D2b,(0x00e24466).l` (0x00E30DEA), the image's only reference.
 * The map gives the cell two bytes (E24466 to the MST_WIRED segment end at
 * E24468); the second is the word-alignment pad.
 *
 * Original address: 0xE24466
 */
boolean MST_$GOT_COLOR = 0;

/*
 * The diskless "partner not responding" console message, formatted by
 * MST_$DISKLESS_INIT (VFMT_$FORMATN into it at 0x00E30DCC) and also
 * referenced from 0x00E0DCD0 / 0x00E0DD48.  0x80 bytes (E24304 up to
 * MST_$ASID_LIST at E24384); zero in the image.
 *
 * Original address: 0xE24304
 */
char MST_$DISKLESS_MSG[MST_DISKLESS_MSG_SIZE];

/*
 * ============================================================================
 * Wiring State
 * ============================================================================
 */

/*
 * Number of wired MST pages
 *
 * Count of MST pages currently wired in memory.  MST_$INIT clears it with
 * `clr.w (0x34,A1)` at 0x00E30C1E, A1 = 0xE7CF0C.
 *
 * Original address: 0xE7CF40 (2 bytes, SAU2 map symbol)
 */
uint16_t MST_$MST_PAGES_WIRED = 0;

/*
 * Maximum MST pages to wire
 *
 * Limit on how many MST pages can be wired.  MST_$INIT computes it into
 * `(0x32,A1)` (0x00E30CE2 .. 0x00E30D14), A1 = 0xE7CF0C.
 *
 * Original address: 0xE7CF3E (2 bytes, SAU2 map symbol)
 */
uint16_t MST_$MST_PAGES_LIMIT = 0;

/*
 * ============================================================================
 * MST_UNWIRED page-table-page allocator state (0xE7CF0C)
 * ============================================================================
 */

/*
 * MST page availability bitmap
 *
 * 12 longwords = 384 bits, one per allocatable page-table page; a SET bit
 * means the page is free.  Read and cleared through A5/A1 = 0xE7CF0C by
 * MST_$INIT (`bclr.b #0x0,(0x3,A1)` at 0x00E30C10, the final clear loop at
 * 0x00E30D86 which walks longword indices 0..11) and by
 * MST_$ALLOC_TABLE_PAGE.
 *
 * Image bytes at 0xE7CF0C..0xE7CF3B are all 0xFF: every page starts free.
 *
 * Original address: 0xE7CF0C (0x30 bytes)
 */
uint32_t MST_$PAGE_AVAIL_BITMAP[MST_$PAGE_AVAIL_BITMAP_LONGS] = {
    0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu,
    0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu,
    0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu
};
_Static_assert(sizeof(MST_$PAGE_AVAIL_BITMAP) == 0x30,
               "MST_$PAGE_AVAIL_BITMAP is E7CF0C..E7CF3B");

/*
 * Page allocation hint
 *
 * Index of the bitmap longword MST_$ALLOC_TABLE_PAGE should start its search
 * at.  MST_$INIT seeds it with the first longword the global-segment
 * pre-allocation left partly free (`move.w (-0x12,A6),(0x30,A1)` at
 * 0x00E30D18); MST_$ALLOC_TABLE_PAGE reads and rewrites it.
 *
 * Image bytes at 0xE7CF3C: 00 00.
 *
 * Original address: 0xE7CF3C (2 bytes)
 */
uint16_t MST_$PAGE_ALLOC_HINT = 0;

/*
 * ============================================================================
 * Segment table
 * ============================================================================
 */

/*
 * MST - the segment table proper
 *
 * One word per segment, holding the index of the page-table page that backs
 * that segment.  MST_$INIT wires and zeroes it page by page starting at
 * `movea.l #0xee5800,A2` (0x00E30BFA); MST_$ALLOC_ASID and mst_$va_to_pte
 * index it.
 *
 * The SAU2 map has MST at EE5800 and PIT_PAGES at EE6400, so the table is
 * 0xC00 bytes = 0x600 words.  It lies in the uninitialised VM_TABLES region,
 * which carries no bytes in the image (`gsk read 0x00EE5800` fails), so the
 * initial contents are whatever MST_$INIT writes.
 *
 * Original address: 0xEE5800 (0xC00 bytes)
 */
uint16_t MST[MST_TABLE_ENTRIES] = { 0 };
_Static_assert(sizeof(MST) == 0xC00, "MST is EE5800..EE63FF");

/*
 * TODO(source-qmdl): 0xE7CF42..0xE7CF53, the last 0x12 bytes of the
 * MST_UNWIRED data segment, hold an unnamed nine-word table
 * { 0, 0, 1, 4, 5, 2, 3, 6, 7 } that no instruction in the image references
 * directly (`gsk xrefs to 0x00E7CF42` and 0x00E7CF46 find nothing - it can
 * only be reached through A5).  Not defined here until a use is found.
 */
