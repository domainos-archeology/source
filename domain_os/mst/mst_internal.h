/*
 * MST Internal Header
 *
 * Internal data structures and globals for the MST subsystem.
 * This header should only be included by mst/ source files.
 */

#ifndef MST_INTERNAL_H
#define MST_INTERNAL_H

#include "mst/mst.h"
#include "uid/uid.h"
#include "mmu/mmu.h"
#include "mmap/mmap.h"
#include "proc1/proc1.h"
#include "area/area.h"
#include "ast/ast.h"

/*
 * ============================================================================
 * CPU Type (from mmu)
 * ============================================================================
 */

/*
 * M68020 - CPU type flag (16-bit word, declared by mmu/mmu.h).
 * The MST code tests only the first (most significant, big-endian) byte
 * of this word for its sign: "tst.b M68020 / bmi".  Testing the sign of
 * the whole 16-bit word is equivalent on any byte order, so MST code
 * uses MST_M68020_IS_020() rather than reading a byte through a pointer.
 *
 * Address confirmed as 0xE23D2E (source-llz): MMU_$INIT at 0xE23D3A does
 * "lea (-0xe,PC),A5" giving A5 = 0xE23D2E and then tests (A5) as the CPU
 * flag.  0xE23D2A lies inside MMAP_$RMT_LIMIT (0xE23D28, 4 bytes) and was
 * never the flag.
 */
#define MST_M68020_IS_020()  ((int16_t)M68020 < 0)

/*
 * ============================================================================
 * Internal Global Data
 * ============================================================================
 */

/*
 * ASID allocation bitmap - longword access to MST_$ASID_LIST (0xE24384)
 *
 * The eight bytes at 0xE24384 are one Domain Pascal SET.  MST_$ALLOC_ASID
 * locates ASID N at byte ((MST_MAX_ASIDS - 1) | 0x0f) - N) >> 3 == (63-N)>>3,
 * bit N & 7, so the set is a 64-bit quantity whose bit 0 lives in its LAST
 * byte (0xE2438B).  MST_$INIT initialises it with two longword stores -
 * `clr.l (0x00E24384).l` at 0x00E30B98 and `move.l #0x1,(0x00E24388).l` at
 * 0x00E30B9E - and on the big-endian m68k the second one leaves 00 00 00 01,
 * i.e. exactly ASID 0 marked allocated.
 *
 * MST_$ASID_LIST_LONG (0xE24384) and the cell at 0xE24388 are therefore not
 * separate objects: they are the two longwords of MST_$ASID_LIST, and the
 * macro below performs such a store MSB-first so the resulting byte pattern
 * is the m68k one on a host of any byte order.
 */
#define MST_$ASID_LIST_LONG_COUNT 2
#define MST_$ASID_LIST_STORE_LONG(idx, val)                                    \
    do {                                                                       \
        uint32_t mst_$asid_list_v_ = (uint32_t)(val);                          \
        MST_$ASID_LIST[(idx) * 4 + 0] = (uint8_t)(mst_$asid_list_v_ >> 24);    \
        MST_$ASID_LIST[(idx) * 4 + 1] = (uint8_t)(mst_$asid_list_v_ >> 16);    \
        MST_$ASID_LIST[(idx) * 4 + 2] = (uint8_t)(mst_$asid_list_v_ >> 8);     \
        MST_$ASID_LIST[(idx) * 4 + 3] = (uint8_t)(mst_$asid_list_v_);          \
    } while (0)

/*
 * MST page availability bitmap - 0xE7CF0C, 12 longwords (0x30 bytes)
 *
 * Set bit = page available.  The count is pinned by MST_$INIT's final clear
 * loop (`moveq #0xb,D2` at 0x00E30D78 walks longword indices up to 11) and by
 * the SAU2 map: the MST_UNWIRED data segment starts at E7CF0C with size 0x48,
 * and its first named symbol is MST_$MST_PAGES_LIMIT at E7CF3E, leaving
 * E7CF0C..E7CF3B for the bitmap and E7CF3C for the allocation hint.
 *
 * 0xE7CF0C is also this module's A5 data base (`lea (0xe7cf0c).l,A5` at
 * 0x00E43988 in MST_$MAPS), so the bitmap sits at A5+0, the hint at A5+0x30,
 * MST_$MST_PAGES_LIMIT at A5+0x32 and MST_$MST_PAGES_WIRED at A5+0x34.
 */
#define MST_$PAGE_AVAIL_BITMAP_LONGS 12
extern uint32_t MST_$PAGE_AVAIL_BITMAP[MST_$PAGE_AVAIL_BITMAP_LONGS];
extern uint16_t MST_$PAGE_ALLOC_HINT;    /* 0xE7CF3C: search hint (word) */

#define status_$pmap_vm_resources_exhausted 0x0004000e

/*
 * ============================================================================
 * Internal Helper Functions
 * ============================================================================
 */

/*
 * mst_$init_table_page - Initialize a freshly allocated page table page
 *
 * Allocates a physical page with WP_$CALLOC, installs it in the MMU at
 * page_addr & 0xFFFFFC00 with flags 0x16, and zeroes the 0x400-byte page.
 * Called by MST_$ALLOC_TABLE_PAGE (0xE43FBE), which ignores the result.
 *
 * Returns: the physical page number returned by WP_$CALLOC (D0).
 *
 * Original address: 0x00E42CEC
 */
uint32_t mst_$init_table_page(uintptr_t page_addr);

/*
 * MST_$ALLOC_TABLE_PAGE - Allocate a page table page for a segment
 *
 * Searches the MST page availability bitmap for a free page, marks it
 * as used, initializes it, and increments the wired page count.
 *
 * Original address: 0x00E43F40
 */
status_$t MST_$ALLOC_TABLE_PAGE(uint16_t asid, uint16_t flags, uint16_t *table_ptr);

/*
 * mst_$alloc_segs - Internal segment allocation and mapping
 *
 * Core internal function called by all MST_$MAP* variants.
 * Finds free segments in the address space, allocates page table
 * pages as needed, and sets up the MST entries for the mapping.
 *
 * Returns the mapped virtual address in A0 register.
 *
 * Original address: 0x00E43182
 *
 * access_rights (its 9th argument, at A6+0x22) and direction (its 10th, at
 * A6+0x24) are Pascal BYTEs: the body tests them with `tst.b (0x22,A6)`
 * (0x00E43234, 0x00E43288) and `tst.b (0x24,A6)` (0x00E432F0, 0x00E4331A),
 * i.e. the even/high byte of each word slot, which is where the callers'
 * `st -(SP)` / `move.b Dn,-(SP)` pushes land.
 */
void *mst_$alloc_segs(uint32_t addr_hint, uid_t *uid, uint32_t start_va, uint32_t length,
                      uint32_t area_size, int16_t asid, uint16_t area_id, uint16_t touch_count,
                      uint8_t access_rights, boolean direction, void *map_info,
                      status_$t *status);

/*
 * mst_$va_to_pte - Look up page table entry for a virtual address
 *
 * Translates an ASID and virtual address into a pointer to the
 * corresponding page table entry. Also returns protection bits.
 *
 * Original address: 0x00E4411C
 */
void mst_$va_to_pte(uint16_t asid, uint32_t va, uint16_t *prot_out, void **entry_out,
                    status_$t *status);

#endif /* MST_INTERNAL_H */
