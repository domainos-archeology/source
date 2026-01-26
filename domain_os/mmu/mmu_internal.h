/*
 * MMU Internal Header
 *
 * Internal data structures, helpers, and globals for the MMU subsystem.
 * This header should only be included by mmu/ source files.
 */

#ifndef MMU_INTERNAL_H
#define MMU_INTERNAL_H

#include "mmu.h"
#include "cache/cache.h"
#include "mmap/mmap.h"
#include "proc1/proc1.h"

/*
 * ============================================================================
 * Internal Global Data
 * ============================================================================
 */

/*
 * Entry point address for CACHE_$CLEAR
 * Used by MMU_$INIT to patch cache clear on 68010 CPUs.
 */
extern uint16_t CACHE_$CLEAR_ENTRY;

/*
 * Power control byte for MMU power register updates.
 * Located at 0xE218D5 (m68k).
 */
extern uint8_t MMU_POWER_CONTROL_BYTE;

/*
 * ============================================================================
 * Internal Helper Functions
 * ============================================================================
 */

/*
 * mmu_$installi - Install a mapping (internal)
 *
 * Low-level installation function called with interrupts disabled
 * and PTT access enabled.
 *
 * Parameters:
 *   ppn - Physical page number
 *   va - Virtual address
 *   packed_info - Pre-packed ASID/protection/VA info
 */
void mmu_$installi(uint16_t ppn, uint32_t va, uint32_t packed_info);

/*
 * mmu_$remove_internal - Remove a mapping (internal)
 *
 * Removes a single PPN from its hash chain. Called with interrupts
 * disabled and PTT access enabled.
 *
 * Parameters:
 *   ppn - Physical page number to remove
 */
void mmu_$remove_internal(uint16_t ppn);

/*
 * mmu_$remove_pmape - Remove PMAPE entry and update hash chain
 *
 * Called with interrupts disabled and PTT access enabled.
 *
 * Parameters:
 *   ppn - Physical page number to remove
 */
void mmu_$remove_pmape(uint16_t ppn);

/*
 * mmu_$unlink_from_hash - Unlink entry from hash chain
 *
 * Updates the hash chain to remove a PPN. Called with interrupts
 * disabled and PTT access enabled.
 *
 * Parameters:
 *   ppn - Physical page number to unlink
 *   prev_offset - Offset of previous entry in chain (0 if head)
 *   pmape_val - Current PMAPE value
 *   ptt_entry - Pointer to PTT entry
 *   pmape - Pointer to PMAPE entry
 */
void mmu_$unlink_from_hash(uint16_t ppn, uint16_t prev_offset,
                            uint32_t pmape_val, uint16_t *ptt_entry,
                            uint32_t *pmape);

#endif /* MMU_INTERNAL_H */
