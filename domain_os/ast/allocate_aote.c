/*
 * ast_$allocate_aote - Take an AOTE from the free list or evict one
 *
 * Three stages, each ending as soon as an entry is obtained:
 *
 *   1. pop the free list (0x00E01D74..0x00E01D86);
 *   2. scan SEVEN entries on from AST_$AOTE_SCAN_POS (moveq #6 / dbf).
 *      A "busy" entry (flags bit 6) is given a second chance by clearing
 *      the bit.  An entry that is not in transition, has a zero reference
 *      count and no ASTEs (status_flags == 0) is deactivated on the spot
 *      with ast_$process_aote.  One that still has ASTEs is remembered as
 *      the best or second-best candidate (fewest ASTEs; on a tie at one
 *      ASTE, the one whose single ASTE holds fewer pages).  After the scan
 *      the two candidates are tried in that order (0x00E01E70..0x00E01E9C);
 *   3. a last-resort sweep of 2*AST_$SIZE_AOT entries (0x00E01EA0..
 *      0x00E01EF8) that deactivates the first entry it can, counting the
 *      event in AST_$ALLOC_WORST_AOT.
 *
 * If all three fail the system is crashed with "no replaceable aste's".
 * AST_$ALLOC_TOTAL_AOT counts every call.
 *
 * Returns: the AOTE (A0).  Stages 2 and 3 leave AST_$AOTE_SCAN_POS on the
 * entry they took; stage 1 and the candidate retries do not move it.
 *
 * Original address: 0x00E01D66 (434 bytes), A5 = 0xE1DC80 (AST_ block):
 *   (0x3EC,A5) free list head        (0x3F0,A5) AST_$AOTE_SCAN_POS
 *   (0x3F4,A5) AST_$AOTE_LIMIT       (0x43C,A5) AST_$ALLOC_WORST_AOT
 *   (0x440,A5) AST_$ALLOC_TOTAL_AOT  (0x46A,A5) AST_$FREE_AOTES
 *   (0x46E,A5) AST_$SIZE_AOT
 * Frame: (-0xC,A6) second candidate, (-0x10,A6) best candidate,
 *        (-0x14,A6) status from ast_$process_aote.
 */

#include "ast/ast_internal.h"

/*
 * Status cell passed to CRASH_SYSTEM by `pea (0x1a,PC)` at 0x00E01EFC
 * (-> 0x00E01F18, jsr CRASH_SYSTEM at 0x00E01F00).  Image bytes
 * 80 03 00 03: "no replaceable aste's" with the fatal bit.  The same cell
 * is used by AST_$ALLOCATE_ASTE (0x00E02060).
 */
static const status_$t ast_$no_replaceable_astes_00e01f18 = 0x80030003;

/*
 * AOTE management cells that ast/ast.h does not export.  The free-list
 * head and count are shared with ast/release_aote.c, which spells them the
 * same way; the scan position and the two counters are only used here.
 * AST_$AOTE_LIMIT (0x3F4) and AST_$SIZE_AOT (0x46E) come from ast/ast.h.
 */
#if defined(ARCH_M68K)
#define AST_$FREE_AOTE_HEAD   (*(aote_t **)0xE1E06C)    /* A5+0x3EC */
#define AST_$AOTE_SCAN_POS    (*(aote_t **)0xE1E070)    /* A5+0x3F0 */
#define AST_$FREE_AOTES       (*(uint16_t *)0xE1E0EA)   /* A5+0x46A */
#define AST_$ALLOC_WORST_AOT  (*(uint32_t *)0xE1E0BC)   /* A5+0x43C */
#define AST_$ALLOC_TOTAL_AOT  (*(uint32_t *)0xE1E0C0)   /* A5+0x440 */
#define AOTE_ARRAY_START      ((aote_t *)0xEC7B60)      /* `AOT` in the map */
#else
#define AST_$FREE_AOTE_HEAD   ast_$free_aote_head
#define AST_$AOTE_SCAN_POS    ast_$aote_scan_pos
#define AST_$FREE_AOTES       ast_$free_aotes
#define AST_$ALLOC_WORST_AOT  ast_$alloc_worst_aot
#define AST_$ALLOC_TOTAL_AOT  ast_$alloc_total_aot
#define AOTE_ARRAY_START      aote_array_start
#endif

/*
 * `lea (0xc0,A2),A2` steps one AOTE; aote_t is 0xC0 bytes on the target
 * (asserted in ast/ast.h), so the C steps by element.
 */

aote_t *ast_$allocate_aote(void)
{
    aote_t *aote;               /* A2 */
    aote_t *best;               /* (-0x10,A6) */
    aote_t *second;             /* (-0xC,A6) */
    aote_t *cand;               /* A2 in the retry loop */
    status_$t local_status;     /* (-0x14,A6) */
    int16_t scan_count;         /* D2 */
    uint16_t sf;                /* D1w: aote->status_flags */

    /* 0x00E01D74..0x00E01D86: pop the free list */
    if (AST_$FREE_AOTE_HEAD != NULL) {
        aote = AST_$FREE_AOTE_HEAD;
        AST_$FREE_AOTE_HEAD = aote->hash_next;
        AST_$FREE_AOTES--;
        goto done;
    }

    /* 0x00E01D8A..0x00E01D98 */
    best = NULL;
    second = NULL;
    aote = AST_$AOTE_SCAN_POS;
    scan_count = 6;                             /* seven entries */

    do {
        /* 0x00E01D9E..0x00E01DA8: advance, wrapping at the limit */
        aote = aote + 1;
        if (aote >= AST_$AOTE_LIMIT) {
            aote = AOTE_ARRAY_START;
        }

        /*
         * 0x00E01DAA..0x00E01DB4: move.w (0xbe,A2),D0w loads ref_count in
         * the high byte and flags in the low byte; btst.l #6 is flags bit
         * 6 (AOTE_FLAG_BUSY).
         */
        if (aote->flags & AOTE_FLAG_BUSY) {
            aote->flags &= (uint8_t)~AOTE_FLAG_BUSY;    /* bclr.b #6,(0xbf,A2) */
            goto next_scan;
        }
        /* 0x00E01DBE: tst.b D0b / bmi - flags bit 7 (in transition) */
        if ((int8_t)aote->flags < 0) {
            goto next_scan;
        }
        /* 0x00E01DC4: tst.b (0xbe,A2) */
        if (aote->ref_count != 0) {
            goto next_scan;
        }

        /* 0x00E01DCC: tst.w (0xbc,A2) - number of ASTEs */
        if (aote->status_flags == 0) {
            /* 0x00E01DD2..0x00E01DEC: clr.w (flags1) + clr.l (flags2,
             * flags3) push three FALSE bytes */
            ast_$process_aote(aote, 0, 0, 0, &local_status);
            if (local_status != status_$ok) {
                goto next_scan;
            }
            goto found_scan;
        }

        /*
         * 0x00E01DF0..0x00E01E34: does this entry displace the best
         * candidate?  Yes when there is none, or when it has fewer ASTEs,
         * or when both have exactly one ASTE and the best's ASTE holds
         * MORE pages than this one's (cmp.b (0x10,A4),D0b / bls at
         * 0x00E01E26).
         */
        if (best == NULL || best->status_flags > aote->status_flags) {
            second = best;
            best = aote;
            goto next_scan;
        }
        sf = aote->status_flags;
        if (best->status_flags == sf && sf == 1 &&
            best->aste_list->page_count > aote->aste_list->page_count) {
            second = best;
            best = aote;
            goto next_scan;
        }

        /*
         * 0x00E01E36..0x00E01E64: otherwise does it displace the second
         * candidate?  Same rule against `second`.
         */
        if (second == NULL) {
            second = aote;
            goto next_scan;
        }
        if (sf < second->status_flags) {
            second = aote;
            goto next_scan;
        }
        if (sf != second->status_flags) {
            goto next_scan;
        }
        if (sf != 1) {
            goto next_scan;
        }
        if (second->aste_list->page_count > aote->aste_list->page_count) {
            second = aote;
        }

next_scan:
        /* 0x00E01E68: dbf D2w */
        ;
    } while (scan_count-- != 0);

    /* 0x00E01E6C */
    AST_$AOTE_SCAN_POS = aote;

    /*
     * 0x00E01E70..0x00E01E9C: try the best candidate, then the second.
     * `lea (0x4,A6),A3` / `movea.l (-0x14,A3),A2` reads A6-0x10 (best)
     * first and `addq.l #0x4,A3` moves on to A6-0xC (second).  A success
     * here returns WITHOUT moving the scan position.
     */
    cand = best;
    if (cand != NULL) {
        ast_$process_aote(cand, 0, 0, 0, &local_status);
        if (local_status == status_$ok) {
            aote = cand;
            goto done;
        }
    }
    cand = second;
    if (cand != NULL) {
        ast_$process_aote(cand, 0, 0, 0, &local_status);
        if (local_status == status_$ok) {
            aote = cand;
            goto done;
        }
    }

    /*
     * 0x00E01EA0..0x00E01EF8: sweep 2 * AST_$SIZE_AOT entries.  The count
     * is a 16-bit `add.w D0w,D0w / subq.w #1`; bmi skips straight to the
     * crash.
     */
    aote = AST_$AOTE_SCAN_POS;
    scan_count = (int16_t)((int16_t)(AST_$SIZE_AOT + AST_$SIZE_AOT) - 1);
    if (scan_count >= 0) {
        do {
            aote = aote + 1;
            if (aote >= AST_$AOTE_LIMIT) {
                aote = AOTE_ARRAY_START;
            }

            /* 0x00E01EC2..0x00E01ED2: same busy second chance */
            if (aote->flags & AOTE_FLAG_BUSY) {
                aote->flags &= (uint8_t)~AOTE_FLAG_BUSY;
            } else {
                ast_$process_aote(aote, 0, 0, 0, &local_status);
                if (local_status == status_$ok) {
                    AST_$ALLOC_WORST_AOT++;             /* 0x00E01EEE */
                    goto found_scan;
                }
            }
            /* 0x00E01EF8: dbf D2w */
        } while (scan_count-- != 0);
    }

    /* 0x00E01EFC..0x00E01F06: nothing replaceable */
    CRASH_SYSTEM(&ast_$no_replaceable_astes_00e01f18);
    aote = NULL;                                        /* suba.l A2,A2 */
    goto done;

found_scan:
    /* 0x00E01EF2 */
    AST_$AOTE_SCAN_POS = aote;

done:
    /* 0x00E01F08..0x00E01F0C */
    AST_$ALLOC_TOTAL_AOT++;
    return aote;
}
