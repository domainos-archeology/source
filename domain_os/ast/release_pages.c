/*
 * AST_$RELEASE_PAGES - Release pages from a segment
 *
 * Releases mapped pages from an ASTE, optionally returning them
 * to the process's page pool.
 *
 * Parameters:
 *   aste - ASTE to release pages from
 *   return_to_pool - If negative, return pages to process pool
 *
 * Original address: 0x00e06f88
 */

#include "ast.h"
#include "mmu/mmu.h"
#include "mmap/mmap.h"

void AST_$RELEASE_PAGES(aste_t *aste, int8_t return_to_pool)
{
    uint16_t *segmap_ptr;
    uint32_t ppn;
    uint16_t remove_count;
    uint32_t remove_list[32];
    int i;

    remove_count = 0;

    /* Get segment map pointer */
    segmap_ptr = (uint16_t *)((uint32_t)aste->seg_index * 0x80 + SEGMAP_BASE);

    ML_$LOCK(PMAP_LOCK_ID);

    /* Scan all 32 pages in segment */
    for (i = 0; i < 32; i++) {
        uint16_t entry_hi = segmap_ptr[0];
        uint16_t entry_lo = segmap_ptr[1];

        /* Check if page is installed and wired */
        if ((entry_hi & 0x4000) != 0 && (entry_hi & 0x2000) != 0) {
            ppn = entry_lo;

            /* Check if page has no references */
            if (*(int8_t *)(ppn * 0x10 + PMAPE_BASE) == 0) {
                /* Clear wired bit */
                *(uint8_t *)segmap_ptr &= 0xDF;

                /* Add to removal list */
                remove_list[remove_count] = ppn;
                remove_count++;
            } else {
                /* Page has references - just unmap it */
                *(uint8_t *)segmap_ptr &= 0xDF;
                MMU_$REMOVE(ppn);
            }
        }

        segmap_ptr += 2;  /* Move to next entry (4 bytes) */
    }

    /* Remove the collected pages from MMU */
    if (remove_count != 0) {
        MMU_$REMOVE_LIST(remove_list, remove_count);

        /* Return to process pool if requested */
        if (return_to_pool < 0) {
            MMAP_$RELEASE_PAGES(PROC1_$CURRENT, remove_list, remove_count);
        }
    }

    ML_$UNLOCK(PMAP_LOCK_ID);
}
