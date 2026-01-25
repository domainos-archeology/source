/*
 * AST_$INVALIDATE_PAGE - Invalidate a single page mapping
 *
 * Removes a page from the MMU mappings and updates the segment map
 * to indicate the page is no longer resident.
 *
 * Original address: 0x00e00f16
 */

#include "ast_internal.h"
#include "mmu/mmu.h"
#include "mmap/mmap.h"

void AST_$INVALIDATE_PAGE(aste_t *aste, uint32_t *segmap_entry, uint32_t ppn)
{
    mmape_t *pmape;

    /* Calculate MMAPE address: 0xEB4800 + ppn * 16 */
    pmape = (mmape_t *)((uintptr_t)MMAPE_BASE + 0x2000 + ppn * sizeof(mmape_t));

    /* If page was installed in MMU, remove it */
    if ((*segmap_entry & SEGMAP_FLAG_INSTALLED) != 0) {
        *segmap_entry &= ~SEGMAP_FLAG_INSTALLED;
        MMU_$REMOVE(ppn);
    }

    /* Clear the in-use flag */
    *segmap_entry &= ~SEGMAP_FLAG_IN_USE;

    /* Update segment map entry with disk address from PMAPE */
    *segmap_entry = (*segmap_entry & 0xFF800000) | pmape->disk_addr;

    /* Free the page and remove from MMAP (0xEB2800 + ppn * 16) */
    MMAP_$FREE_REMOVE((mmape_t *)((uintptr_t)MMAPE_BASE + ppn * sizeof(mmape_t)), ppn);

    /* Decrement page count */
    aste->page_count--;
}
