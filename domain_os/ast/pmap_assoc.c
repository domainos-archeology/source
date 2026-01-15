/*
 * AST_$PMAP_ASSOC - Associate a physical page with a segment map entry
 *
 * Links a physical page to a virtual address via the segment map.
 * Handles unmapping any existing page and updating PMAPE entries.
 *
 * Parameters:
 *   aste - ASTE for the segment
 *   page - Page number within segment (0-31)
 *   ppn - Physical page number to associate
 *   flags1 - Operation flags
 *   flags2 - Additional flags
 *   status - Status return
 *
 * Original address: 0x00e042b0
 */

#include "ast/ast_internal.h"
#include "misc/misc.h"
#include "mmu/mmu.h"
#include "mmap/mmap.h"

void AST_$PMAP_ASSOC(aste_t *aste, uint16_t page, uint32_t ppn,
                     uint16_t flags1, uint16_t flags2, status_$t *status)
{
    aote_t *aote;
    uint32_t *segmap_ptr;
    int segmap_offset;
    uint16_t old_entry;
    uint32_t old_ppn;
    int pmape_offset;

    *status = status_$ok;

    aote = aste->aote;

    /* Calculate segment map offset */
    segmap_offset = (int)(page << 2) + (uint32_t)aste->seg_index * 0x80;
    segmap_ptr = (uint32_t *)(SEGMAP_BASE + segmap_offset - 0x80);

    /* Wait for any pages in transition */
    while (*(int16_t *)segmap_ptr < 0) {
        ast_$wait_for_page_transition();
    }

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

        /* Check if page has references */
        if (*(int8_t *)(PMAPE_BASE + pmape_offset) != 0) {
            *status = 0x50007;  /* Page has references */
            return;
        }

        /* Clear installed bit */
        *(uint8_t *)segmap_ptr &= 0xBF;

        /* Clear PPN in segment map */
        *segmap_ptr &= 0xFF800000;

        /* Copy disk address from PMAPE */
        *segmap_ptr |= *(uint32_t *)(PMAPE_BASE + pmape_offset + 0x0C);

        /* Free the old page */
        MMAP_$FREE_REMOVE((uint8_t *)(PMAPE_BASE + pmape_offset), old_ppn);

        aste->page_count--;
    } else {
        /* Page not installed - check if we need disk block */
        if ((*segmap_ptr & SEGMAP_DISK_ADDR_MASK) == 0) {
            /* No disk block and local object */
            if (*(int8_t *)((char *)aote + 0xB9) >= 0) {
                *status = status_$pmap_bad_assoc;
                if ((int16_t)flags2 >= 0) {
                    return;
                }
            }
        }
    }

    /* Validate new PPN */
    if (ppn == 0) {
        CRASH_SYSTEM(OS_PMAP_mismatch_err);
    }

    /* Set up new page mapping */
    if (ppn > 0x1FF && ppn < 0x1000) {
        pmape_offset = ppn * 0x10;

        /* Check that page is not already installed */
        if (*(int8_t *)(PMAPE_BASE + pmape_offset + 5) < 0) {
            CRASH_SYSTEM(OS_MMAP_bad_install);
        }

        /* Set up PMAPE entry */
        *(uint16_t *)(PMAPE_BASE + pmape_offset + 2) = aste->seg_index;
        *(uint8_t *)(PMAPE_BASE + pmape_offset + 5) |= 0x40;
        *(uint8_t *)(PMAPE_BASE + pmape_offset + 1) = (uint8_t)page;
        *(uint8_t *)(PMAPE_BASE + pmape_offset + 9) |= 0x40;
        *(uint8_t *)(PMAPE_BASE + pmape_offset + 9) &= 0x7F;

        /* Store disk address from segment map */
        *(uint32_t *)(PMAPE_BASE + pmape_offset + 0x0C) = *segmap_ptr & SEGMAP_DISK_ADDR_MASK;

        /* Install page if not already referenced */
        if (*(int8_t *)(PMAPE_BASE + pmape_offset) == 0) {
            MMAP_$INSTALL_LIST(&ppn, 1, 0);
        }
    }

    /* Update segment map entry */
    *(uint16_t *)((char *)segmap_ptr + 2) = (uint16_t)ppn;
    *(uint8_t *)segmap_ptr |= 0x40;  /* Set installed */

    /* Update hardware MMU entry */
    *(uint16_t *)(0xFFB802 + ppn * 4) = (*(uint16_t *)(0xFFB802 + ppn * 4) & 0xBFFF) | 0x2000;

    /* Mark as referenced */
    *(uint8_t *)segmap_ptr |= 0x20;

    aste->page_count++;
}
