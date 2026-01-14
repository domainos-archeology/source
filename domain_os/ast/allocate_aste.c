/*
 * AST_$ALLOCATE_ASTE - Allocate an ASTE
 *
 * Allocates an ASTE from the free list or by stealing one
 * from an existing mapping. Uses a multi-pass algorithm:
 * 1. Check free list
 * 2. Scan for unused entries (reference count = 0)
 * 3. Try to steal from less-used entries
 *
 * Original address: 0x00e01f1c
 */

#include "ast.h"
#include "misc/misc.h"

/* Internal function prototypes */
static aste_t* try_free_aste(aste_t *aste, status_$t *status);

aste_t* AST_$ALLOCATE_ASTE(void)
{
    aste_t *aste;
    aste_t *candidate[2];
    aste_t *scan_pos;
    int16_t pass;
    status_$t status;

    /* First check the free list */
    if (AST_$FREE_ASTE_HEAD != NULL) {
        aste = AST_$FREE_ASTE_HEAD;
        AST_$FREE_ASTE_HEAD = aste->next;
        AST_$FREE_ASTES--;
        goto done;
    }

    /* No free entries - need to find one to reuse */
    candidate[0] = NULL;
    candidate[1] = NULL;
    scan_pos = AST_$ASTE_SCAN_POS;

    /* First pass: scan 12 entries looking for unreferenced ASTEs */
    for (pass = 11; pass >= 0; pass--) {
        scan_pos = (aste_t*)((char*)scan_pos + sizeof(aste_t));
        if (scan_pos >= AST_$ASTE_LIMIT) {
            scan_pos = ASTE_BASE;
        }

        /* Check if entry is locked */
        if (scan_pos->flags & ASTE_FLAG_LOCKED) {
            scan_pos->flags &= ~ASTE_FLAG_BUSY;
            continue;
        }

        /* Skip entries with negative flags or non-zero wire count */
        if ((int16_t)scan_pos->flags < 0) continue;
        if (scan_pos->wire_count != 0) continue;

        /* Entry with zero page count can be freed immediately */
        if (scan_pos->page_count == 0) {
            aste = try_free_aste(scan_pos, &status);
            if (status == status_$ok) {
                goto found;
            }
        } else {
            /* Track candidates with lowest page counts */
            if (candidate[0] == NULL || scan_pos->page_count < candidate[0]->page_count) {
                candidate[1] = candidate[0];
                candidate[0] = scan_pos;
            } else if (candidate[1] == NULL || scan_pos->page_count < candidate[1]->page_count) {
                candidate[1] = scan_pos;
            }
        }
    }

    AST_$ASTE_SCAN_POS = scan_pos;

    /* Try the candidates we found */
    for (pass = 1; pass >= 0; pass--) {
        if (candidate[pass] != NULL) {
            aste = try_free_aste(candidate[pass], &status);
            if (status == status_$ok) {
                goto found;
            }
        }
    }

    /* Last resort: full scan of all ASTEs */
    scan_pos = AST_$ASTE_SCAN_POS;
    for (pass = AST_$SIZE_AST * 2 - 1; pass >= 0; pass--) {
        scan_pos = (aste_t*)((char*)scan_pos + sizeof(aste_t));
        if (scan_pos >= AST_$ASTE_LIMIT) {
            scan_pos = ASTE_BASE;
        }

        if (scan_pos->flags & ASTE_FLAG_LOCKED) {
            scan_pos->flags &= ~ASTE_FLAG_BUSY;
            continue;
        }

        aste = try_free_aste(scan_pos, &status);
        if (status == status_$ok) {
            AST_$ALLOC_WORST_AST++;
            goto found;
        }
    }

    /* Failed to allocate - crash */
    CRASH_SYSTEM(Some_ASTE_Error);
    aste = NULL;

found:
    AST_$ASTE_SCAN_POS = scan_pos;

    /* Update counters */
    if (aste->flags & ASTE_FLAG_AREA) {
        AST_$ASTE_AREA_CNT--;
    } else if (aste->flags & ASTE_FLAG_REMOTE) {
        AST_$ASTE_R_CNT--;
    } else {
        AST_$ASTE_L_CNT--;
    }

done:
    AST_$ALLOC_TOTAL_AST++;
    return aste;
}

/*
 * try_free_aste - Try to free an ASTE for reuse
 *
 * Internal helper that attempts to release pages and free an ASTE.
 * Returns the ASTE on success, NULL on failure.
 */
static aste_t* try_free_aste(aste_t *aste, status_$t *status)
{
    /* TODO: Implement page release logic */
    /* This function needs to release any pages held by the ASTE
     * and return it for reuse */
    *status = status_$ok;
    return aste;
}
