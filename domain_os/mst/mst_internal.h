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
 * TODO: mmu/mmu.h places M68020 at 0xE23D2A, but Ghidra labels M68020 at
 * 0xE23D2E (2 bytes); 0xE23D2A lies inside MMAP_$RMT_LIMIT.  The mmu
 * subsystem should confirm the address.
 */
#define MST_M68020_IS_020()  ((int16_t)M68020 < 0)

/*
 * ============================================================================
 * Internal Global Data
 * ============================================================================
 */

/*
 * ASID allocation bitmap (32-bit access views)
 * MST_$ASID_LIST is also accessible as two 32-bit words.
 */
extern uint32_t MST_$ASID_LIST_LONG;    /* First 32 bits of ASID bitmap */
extern uint32_t DAT_00e24388;           /* Second part of ASID bitmap */

/*
 * MST page availability bitmap
 * Tracks which MST table pages are available for allocation.
 * 12 words (384 bits), set bit = page available.
 * Located at 0xE7CF0C (m68k).
 */
extern uint32_t MST_$PAGE_AVAIL_BITMAP[];
extern uint16_t MST_$PAGE_ALLOC_HINT;    /* Search hint for next free word */
extern uint16_t MST_$MST_PAGES_WIRED;    /* Count of wired MST pages */

#define status_$pmap_vm_resources_exhausted 0x0004000e

/*
 * ============================================================================
 * Error Status (from pmap)
 * ============================================================================
 */

extern status_$t PMAP_VM_Resources_exhausted_err;

/*
 * ============================================================================
 * Error Status (internal)
 * ============================================================================
 */

extern status_$t MST_Ref_OutOfBounds_Err;

/*
 * ============================================================================
 * Internal Helper Functions
 * ============================================================================
 */

/*
 * mst_$init_table_page - Initialize a freshly allocated page table page
 *
 * Allocates a physical page, installs it in the MMU at page_addr and
 * zeroes it.  Called by MST_$ALLOC_TABLE_PAGE.
 *
 * Original address: 0x00E42CEC
 * TODO: not yet decompiled.
 */
void mst_$init_table_page(uint32_t page_addr);

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
 */
void *mst_$alloc_segs(uint32_t addr_hint, uid_t *uid, uint32_t start_va, uint32_t length,
                      uint32_t area_size, int16_t asid, uint16_t area_id, uint16_t touch_count,
                      uint8_t access_rights, int16_t direction, void *map_info,
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
