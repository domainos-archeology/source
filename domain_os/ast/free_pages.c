/*
 * AST_$FREE_PAGES - Free pages from a segment
 *
 * Frees pages in a range from a segment's mapping, optionally
 * returning the disk blocks to the BAT (Block Allocation Table).
 *
 * Original address: 0x00e0400c
 */

#include "ast/ast_internal.h"
#include "misc/misc.h"
#include "bat/bat.h"

void AST_$FREE_PAGES(aste_t *aste, int16_t start_page, int16_t end_page, int16_t flags)
{
    uint32_t *segmap_ptr;
    uint32_t disk_addr;
    uint16_t bat_count;
    uint16_t installed_count;
    int16_t remaining;
    status_$t status;
    uint32_t installed_pages[32];
    uint32_t bat_blocks[33];

    ML_$LOCK(PMAP_LOCK_ID);

    bat_count = 0;
    installed_count = 0;

    /* Calculate segment map pointer */
    segmap_ptr = (uint32_t *)((char *)SEGMAP_BASE + (uint32_t)aste->seg_index * 0x80 - 0x80 +
                              (int16_t)(start_page << 2));

    remaining = end_page - start_page + 1;

    while (remaining > 0) {
        /* Wait if page is in transition */
        while ((int16_t)*segmap_ptr < 0) {
            if (installed_count != 0) {
                FUN_00e03fbc();
            }
            FUN_00e00c08();
        }

        if ((*segmap_ptr & SEGMAP_FLAG_IN_USE) == 0) {
            /* Page not installed - get disk address directly */
            disk_addr = *segmap_ptr & SEGMAP_DISK_ADDR_MASK;
            if (disk_addr != 0) {
                goto mark_dirty;
            }
        } else {
            /* Page is installed - get disk address from PMAPE */
            pmape_t *pmape = (pmape_t *)(PMAPE_BASE + 0x2000 +
                              (uint32_t)(uint16_t)*segmap_ptr * sizeof(pmape_t));
            disk_addr = pmape->disk_addr & SEGMAP_DISK_ADDR_MASK;

            /* Track installed page for later removal */
            installed_count++;
            installed_pages[installed_count] = (uint32_t)(uint16_t)*segmap_ptr;

            if (installed_count == 0x20) {
                FUN_00e03fbc();
            }

mark_dirty:
            /* Mark ASTE as dirty */
            aste->flags |= ASTE_FLAG_DIRTY;
        }

        /* Clear segment map entry */
        *segmap_ptr = 0;

        /* If there's a disk block to free, add it to BAT list */
        if (disk_addr != 0 && flags != 0) {
            bat_count++;
            bat_blocks[bat_count] = disk_addr;

            if (bat_count == 0x20) {
                if (installed_count != 0) {
                    FUN_00e03fbc();
                }
                ML_$UNLOCK(PMAP_LOCK_ID);
                BAT_$FREE(&bat_blocks[1], 0x20, flags, 1, &status);
                if (status != status_$ok) {
                    CRASH_SYSTEM(&status);
                }
                ML_$LOCK(PMAP_LOCK_ID);
                bat_count = 0;
            }
        }

        segmap_ptr++;
        remaining--;
    }

    /* Flush any remaining installed pages */
    if (installed_count != 0) {
        FUN_00e03fbc();
    }

    ML_$UNLOCK(PMAP_LOCK_ID);

    /* Free any remaining BAT blocks */
    if (bat_count != 0) {
        BAT_$FREE(&bat_blocks[1], bat_count, flags, 1, &status);
        if (status != status_$ok) {
            CRASH_SYSTEM((const char *)&status);
        }
    }
}
