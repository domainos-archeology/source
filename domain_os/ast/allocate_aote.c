/*
 * ast_$allocate_aote - Allocate a new AOTE from the free list or by eviction
 *
 * Attempts to get an AOTE from the free list first. If none available,
 * scans the AOTE table looking for candidates to evict. Uses a multi-pass
 * algorithm with increasing desperation.
 *
 * Returns: Pointer to allocated AOTE
 *
 * Original address: 0x00e01d66
 */

#include "ast/ast_internal.h"

/* External function prototypes */
extern void CRASH_SYSTEM(const status_$t *status);

/* Error status */
extern status_$t Some_ASTE_Error;

/*
 * AOTE management globals
 */
#if defined(M68K)
#define AST_$FREE_AOTE_HEAD   (*(aote_t **)0xE1E06C)    /* A5+0x3EC */
#define AST_$AOTE_SCAN_POS    (*(aote_t **)0xE1E070)    /* A5+0x3F0 */
#define AST_$AOTE_END         (*(aote_t **)0xE1E074)    /* A5+0x3F4 */
#define AST_$FREE_AOTES       (*(uint16_t *)0xE1E0EA)   /* A5+0x46A */
#define AST_$ALLOC_WORST_AOT  (*(uint32_t *)0xE1E0BC)   /* A5+0x43C */
#define AST_$ALLOC_TOTAL_AOT  (*(uint32_t *)0xE1E0C0)   /* A5+0x440 */
#define AST_$SIZE_AOT         (*(uint16_t *)0xE1E0EE)   /* A5+0x46E */
#define AOTE_ARRAY_START      ((aote_t *)0xEC7B60)
#else
extern aote_t *ast_$free_aote_head;
extern aote_t *ast_$aote_scan_pos;
extern aote_t *ast_$aote_end;
extern uint16_t ast_$free_aotes;
extern uint32_t ast_$alloc_worst_aot;
extern uint32_t ast_$alloc_total_aot;
extern uint16_t ast_$size_aot;
extern aote_t *aote_array_start;
#define AST_$FREE_AOTE_HEAD   ast_$free_aote_head
#define AST_$AOTE_SCAN_POS    ast_$aote_scan_pos
#define AST_$AOTE_END         ast_$aote_end
#define AST_$FREE_AOTES       ast_$free_aotes
#define AST_$ALLOC_WORST_AOT  ast_$alloc_worst_aot
#define AST_$ALLOC_TOTAL_AOT  ast_$alloc_total_aot
#define AST_$SIZE_AOT         ast_$size_aot
#define AOTE_ARRAY_START      aote_array_start
#endif

/* AOTE entry size is 0xC0 (192 bytes) */
#define AOTE_SIZE 0xC0

aote_t *ast_$allocate_aote(void)
{
    aote_t *aote;
    aote_t *candidates[2];
    status_$t local_status;
    int16_t scan_count;
    int16_t i;

    /* First check if there's an AOTE on the free list */
    if (AST_$FREE_AOTE_HEAD != NULL) {
        aote = AST_$FREE_AOTE_HEAD;
        AST_$FREE_AOTE_HEAD = aote->hash_next;
        AST_$FREE_AOTES--;
        goto found;
    }

    /* No free AOTEs - need to evict one */
    candidates[0] = NULL;
    candidates[1] = NULL;

    /* Scan forward from last position, looking for eviction candidates */
    aote = AST_$AOTE_SCAN_POS;
    scan_count = 6;

    do {
        /* Move to next AOTE (wrap around at end) */
        aote = (aote_t *)((char *)aote + AOTE_SIZE);
        if (aote >= AST_$AOTE_END) {
            aote = AOTE_ARRAY_START;
        }

        /* Check BUSY flag (0x40 at offset 0xBF) */
        if ((aote->status_flags & 0x40) != 0) {
            /* Clear BUSY flag for future scans */
            aote->flags &= ~AOTE_FLAG_BUSY;
            goto next_scan;
        }

        /* Check if AOTE is not in-transition and has zero ref count */
        if ((aote->flags & AOTE_FLAG_IN_TRANS) == 0 && aote->ref_count == 0) {
            /* Check if it has no ASTEs (status_flags at 0xBC) */
            if (aote->status_flags == 0) {
                /* Perfect candidate - try to process it */
                ast_$process_aote(aote, 0, 0, 0, &local_status);
                if (local_status == status_$ok) {
                    goto found_scan;
                }
            } else {
                /* Has ASTEs - save as potential candidate */
                if (candidates[0] == NULL ||
                    aote->status_flags < candidates[0]->status_flags) {
                    candidates[1] = candidates[0];
                    candidates[0] = aote;
                } else {
                    /* Check for tiebreaker based on page count */
                    uint16_t count = aote->status_flags;
                    if (count == candidates[0]->status_flags && count == 1) {
                        /* Compare page counts */
                        uint8_t aote_pages = *((uint8_t *)(aote->aste_list) + 0x10);
                        uint8_t cand_pages = *((uint8_t *)(candidates[0]->aste_list) + 0x10);
                        if (aote_pages > cand_pages) {
                            candidates[1] = candidates[0];
                            candidates[0] = aote;
                            goto next_scan;
                        }
                    }
                    /* Save as second candidate */
                    if (candidates[1] == NULL ||
                        count < candidates[1]->status_flags ||
                        (count == candidates[1]->status_flags && count == 1 &&
                         *((uint8_t *)(aote->aste_list) + 0x10) <
                         *((uint8_t *)(candidates[1]->aste_list) + 0x10))) {
                        candidates[1] = aote;
                    }
                }
            }
        }

next_scan:
        scan_count--;
    } while (scan_count != -1);

    /* Update scan position */
    AST_$AOTE_SCAN_POS = aote;

    /* Try the candidates we found */
    for (i = 1; i >= 0; i--) {
        aote = candidates[i];
        if (aote != NULL) {
            ast_$process_aote(aote, 0, 0, 0, &local_status);
            if (local_status == status_$ok) {
                goto found;
            }
        }
    }

    /* Last resort - full scan of entire AOTE table */
    aote = AST_$AOTE_SCAN_POS;
    scan_count = AST_$SIZE_AOT * 2 - 1;
    if (scan_count >= 0) {
        do {
            aote = (aote_t *)((char *)aote + AOTE_SIZE);
            if (aote >= AST_$AOTE_END) {
                aote = AOTE_ARRAY_START;
            }

            if ((aote->status_flags & 0x40) != 0) {
                aote->flags &= ~AOTE_FLAG_BUSY;
            } else {
                ast_$process_aote(aote, 0, 0, 0, &local_status);
                if (local_status == status_$ok) {
                    AST_$ALLOC_WORST_AOT++;
                    goto found_scan;
                }
            }
            scan_count--;
        } while (scan_count != -1);
    }

    /* No AOTE available - crash */
    CRASH_SYSTEM(&Some_ASTE_Error);
    aote = NULL;
    goto found;

found_scan:
    AST_$AOTE_SCAN_POS = aote;

found:
    AST_$ALLOC_TOTAL_AOT++;
    return aote;
}
