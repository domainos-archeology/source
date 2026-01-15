/*
 * AST_$ASSOC_AREA - Associate a physical page with an area segment map entry
 *
 * Low-level function to associate a physical page with a segment map entry
 * for area mappings. Similar to AST_$PMAP_ASSOC but operates directly on
 * segment index rather than ASTE.
 *
 * Parameters:
 *   seg_index - Segment index
 *   page - Page number within segment (0-31)
 *   ppn - Physical page number to associate
 *   status - Status return
 *
 * Original address: 0x00e04542
 */

#include "ast/ast_internal.h"
#include "misc/misc.h"
#include "mmu/mmu.h"
#include "mmap/mmap.h"

void AST_$ASSOC_AREA(uint16_t seg_index, int16_t page, uint32_t ppn, status_$t *status)
{
    int segmap_offset;
    uint32_t *segmap_ptr;
    uint16_t old_entry;
    uint32_t old_ppn;
    int pmape_offset;
    int8_t old_ref_count;

    segmap_offset = (int)(page << 2) + (uint32_t)seg_index * 0x80;
    segmap_ptr = (uint32_t *)(SEGMAP_BASE + segmap_offset - 0x80);

    /* Wait for any pages in transition */
    while (*(int16_t *)segmap_ptr < 0) {
        ast_$wait_for_page_transition();
    }

    old_ref_count = 0;
    old_entry = *(uint16_t *)segmap_ptr;

    /* Check if page is already installed */
    if ((old_entry & 0x4000) != 0) {
        /* Page is installed - need to remove it first */
        old_ppn = *(uint16_t *)((char *)segmap_ptr + 2);
        pmape_offset = old_ppn * 0x10;

        /* Check if page is wired */
        if ((old_entry & 0x2000) != 0) {
            /* Clear wired bit and remove from MMU */
            *(uint8_t *)segmap_ptr &= 0xDF;
            MMU_$REMOVE(old_ppn);
        }

        /* Clear installed bit */
        *(uint8_t *)segmap_ptr &= 0xBF;

        /* Save reference count */
        old_ref_count = *(int8_t *)(PMAPE_BASE + pmape_offset);

        /* Clear PPN in segment map */
        *segmap_ptr &= 0xFF800000;

        /* Copy disk address from PMAPE */
        *segmap_ptr |= *(uint32_t *)(PMAPE_BASE + pmape_offset + 0x0C);

        /* Free the old page */
        MMAP_$FREE_REMOVE((mmape_t *)(PMAPE_BASE + pmape_offset), old_ppn);

        /* Decrement page count in ASTE area */
        (*(int8_t *)(seg_index * 0x14 + 0xEC53FC))--;
    }

    /* Validate new PPN */
    if (ppn == 0) {
        CRASH_SYSTEM(&OS_PMAP_mismatch_err);
    }

    /* Set up new page mapping */
    if (ppn > 0x1FF && ppn < 0x1000) {
        pmape_offset = ppn * 0x10;

        /* Check that page is not already installed */
        if (*(int8_t *)(PMAPE_BASE + pmape_offset + 5) < 0) {
            CRASH_SYSTEM(OS_MMAP_bad_install);
        }

        /* Set up PMAPE entry */
        *(int8_t *)(PMAPE_BASE + pmape_offset) = old_ref_count;
        *(uint16_t *)(PMAPE_BASE + pmape_offset + 2) = seg_index;
        *(uint8_t *)(PMAPE_BASE + pmape_offset + 5) |= 0x40;
        *(uint8_t *)(PMAPE_BASE + pmape_offset + 1) = (uint8_t)page;
        *(uint16_t *)(PMAPE_BASE + pmape_offset + 8) |= 0xC0;

        /* Store disk address from segment map */
        *(uint32_t *)(PMAPE_BASE + pmape_offset + 0x0C) = *segmap_ptr & 0x7FFFFF;

        /* Install page if not referenced */
        if (old_ref_count == 0) {
            MMAP_$INSTALL_LIST(&ppn, 1, 0);
        }
    }

    /* Update segment map entry */
    *(uint16_t *)((char *)segmap_ptr + 2) = (uint16_t)ppn;
    *(uint8_t *)segmap_ptr |= 0x40;  /* Set installed */

    /* Update hardware MMU entry */
    *(uint16_t *)(0xFFB802 + ppn * 4) = (*(uint16_t *)(0xFFB802 + ppn * 4) & 0xBFFF) | 0x2000;

    /* Increment page count in ASTE area */
    (*(int8_t *)(seg_index * 0x14 + 0xEC53FC))++;

    *status = status_$ok;
}
