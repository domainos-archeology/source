/*
 * AST_$REMOVE_CORRUPTED_PAGE - Remove a corrupted page from the system
 *
 * Handles removal of a page that has been detected as corrupted.
 * If the page can be cleanly removed, it's invalidated. Otherwise,
 * the UID is saved for later trouble handling.
 *
 * Parameters:
 *   ppn - Physical page number of the corrupted page
 *
 * Returns: 0xFF if page was successfully removed, 0 otherwise
 *
 * Original address: 0x00e07276
 */

#include "ast/ast_internal.h"
#include "proc1/proc1.h"

uint8_t AST_$REMOVE_CORRUPTED_PAGE(uint32_t ppn)
{
    uint8_t result;
    uint8_t ast_locked;
    uint8_t pmap_locked;
    int pmape_offset;
    uint16_t seg_index;
    int segmap_offset;
    uint16_t segmap_entry;
    aste_t *aste;

    result = 0;

    /* Check if locks are held */
    ast_locked = PROC1_$TST_LOCK(AST_LOCK_ID);
    pmap_locked = PROC1_$TST_LOCK(PMAP_LOCK_ID);

    /* Only proceed if not already locked and PPN is in valid range */
    if ((int8_t)(ast_locked | pmap_locked | (-(ppn < 0x200))) >= 0 && ppn < 0x1000) {
        pmape_offset = ppn * 0x10;

        /* Get segment index from PMAPE */
        seg_index = *(uint16_t *)(PMAPE_BASE + pmape_offset + 2);

        /* Check if page is installed and has a valid segment */
        if (*(int8_t *)(PMAPE_BASE + pmape_offset + 5) < 0 && seg_index != 0) {
            /* Calculate segment map offset */
            uint8_t page_offset = *(uint8_t *)(PMAPE_BASE + pmape_offset + 1);
            segmap_offset = (int)(int16_t)(page_offset << 2) + (uint32_t)seg_index * 0x80;

            /* Check segment map entry */
            if (*(int16_t *)(SEGMAP_BASE + segmap_offset - 0x80) >= 0) {
                segmap_entry = *(uint16_t *)(SEGMAP_BASE + segmap_offset - 0x80);

                if ((segmap_entry & 0x4000) != 0) {
                    /* Page is installed - check if it can be cleanly removed */
                    uint16_t hw_entry = *(uint16_t *)(0xFFB802 + ppn * 4);
                    uint8_t pmape_flags = *(uint8_t *)(PMAPE_BASE + pmape_offset + 9);

                    if ((hw_entry & 0x4000) == 0 && (pmape_flags & 0x40) == 0) {
                        /* Page can be safely invalidated */
                        aste = (aste_t *)(seg_index * 0x14 + 0xEC53EC);
                        AST_$INVALIDATE_PAGE(aste,
                                             (uint32_t *)(SEGMAP_BASE + segmap_offset - 0x80),
                                             ppn);
                        result = 0xFF;
                    } else {
                        /* Page is dirty or modified - save UID for later */
                        aote_t *aote = *(aote_t **)(seg_index * 0x14 + 0xEC53F0);
                        AST_$SAVE_CLOBBERED_UID((uid_t *)((char *)aote + 0x10));
                    }
                }
            }
        }
    }

    return result;
}
