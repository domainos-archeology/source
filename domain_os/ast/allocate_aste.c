/*
 * AST_$ALLOCATE_ASTE - Take an ASTE from the free list or steal one
 *
 * Three stages, each ending as soon as an entry is obtained:
 *
 *   1. pop the free list (0x00E01F2A..0x00E01F3C);
 *   2. scan TWELVE entries on from AST_$ASTE_SCAN_POS (moveq #0xb / dbf).
 *      An entry with flags bit 14 set is given a second chance by clearing
 *      the bit (`bclr.b #0x6,(0x12,A2)` - bit 6 of the HIGH byte of the
 *      flags word, i.e. 0x4000, not the 0x0040 "busy" bit).  An entry not
 *      in transition, unwired and holding no pages is deactivated on the
 *      spot; one holding pages is remembered as the best or second-best
 *      candidate (fewest pages).  After the scan the two candidates are
 *      tried in that order (0x00E01FDC..0x00E02004);
 *   3. a last-resort sweep of 2*AST_$SIZE_AST entries (0x00E02008..
 *      0x00E0205C) that deactivates the first entry it can, counting the
 *      event in AST_$ALLOC_WORST_AST.
 *
 * If all three fail the system is crashed.  The stolen entry's flags
 * decide which of the AREA / remote / local ASTE counters goes down;
 * AST_$ALLOC_TOTAL_AST counts every call.
 *
 * Original address: 0x00E01F1C (386 bytes), A5 = 0xE1DC80 (AST_ block):
 *   (0x3F8,A5) AST_$FREE_ASTE_HEAD  (0x3FC,A5) AST_$ASTE_SCAN_POS
 *   (0x400,A5) AST_$ASTE_LIMIT      (0x444,A5) AST_$ALLOC_WORST_AST
 *   (0x448,A5) AST_$ALLOC_TOTAL_AST (0x468,A5) AST_$FREE_ASTES
 *   (0x470,A5) AST_$SIZE_AST        (0x472/0x474/0x476,A5) the three counts
 * Frame: (-0x4,A6) second candidate, (-0x8,A6) best candidate,
 *        (-0xC,A6) status from AST_$DEACTIVATE_SEGMENT.
 */

#include "ast/ast_internal.h"
#include "misc/misc.h"

/*
 * Status cell passed to CRASH_SYSTEM by `pea (-0x14a,PC)` at 0x00E02060
 * (-> 0x00E01F18, jsr CRASH_SYSTEM at 0x00E02064).  Image bytes
 * 80 03 00 03: "no replaceable aste's" with the fatal bit.  The same cell
 * is used by ast_$allocate_aote (0x00E01EFC).
 */
static const status_$t ast_$no_replaceable_astes_00e01f18 = 0x80030003;

/*
 * `lea (0x14,A2),A2` steps one ASTE; aste_t is 0x14 bytes on the target
 * (asserted in ast/ast.h), so the C steps by element.
 */

aste_t *AST_$ALLOCATE_ASTE(void)
{
    aste_t *aste;               /* A2 */
    aste_t *best;               /* (-0x8,A6) */
    aste_t *second;             /* (-0x4,A6) */
    aste_t *cand;               /* A2 in the retry loop */
    status_$t local_status;     /* (-0xC,A6) */
    int16_t scan_count;         /* D2 */
    uint16_t flags;             /* D0w */

    /* 0x00E01F2A..0x00E01F3C: pop the free list */
    if (AST_$FREE_ASTE_HEAD != NULL) {
        aste = AST_$FREE_ASTE_HEAD;
        AST_$FREE_ASTE_HEAD = aste->next;
        AST_$FREE_ASTES--;
        goto done;
    }

    /* 0x00E01F40..0x00E01F4E */
    best = NULL;
    second = NULL;
    aste = AST_$ASTE_SCAN_POS;
    scan_count = 0xB;                           /* twelve entries */

    do {
        /* 0x00E01F54..0x00E01F5E: advance, wrapping at the limit */
        aste = aste + 1;
        if (aste >= AST_$ASTE_LIMIT) {
            aste = ASTE_BASE;
        }

        /* 0x00E01F60..0x00E01F70: btst.l #0xe / bclr.b #0x6,(0x12,A2) */
        flags = aste->flags;
        if (flags & ASTE_FLAG_LOCKED) {
            aste->flags &= (uint16_t)~ASTE_FLAG_LOCKED;
            goto next_scan;
        }
        /* 0x00E01F72: tst.w D0w / bmi - in transition */
        if ((int16_t)flags < 0) {
            goto next_scan;
        }
        /* 0x00E01F76: tst.b (0x11,A2) */
        if (aste->wire_count != 0) {
            goto next_scan;
        }

        /* 0x00E01F7C: tst.b (0x10,A2) */
        if (aste->page_count == 0) {
            /* 0x00E01F82..0x00E01F98: `clr.l -(SP)` pushes both byte
             * flags (purge, keep) as zero */
            AST_$DEACTIVATE_SEGMENT(aste, 0, 0, &local_status);
            if (local_status != status_$ok) {
                goto next_scan;
            }
            goto found_scan;
        }

        /*
         * 0x00E01F9C..0x00E01FBA: displace the best candidate when there
         * is none or it holds MORE pages than this entry (cmp.b
         * (0x10,A2),D0b / bls).
         */
        if (best == NULL || best->page_count > aste->page_count) {
            second = best;
            best = aste;
            goto next_scan;
        }
        /* 0x00E01FBC..0x00E01FD0: else the second candidate, same rule */
        if (second == NULL || second->page_count > aste->page_count) {
            second = aste;
        }

next_scan:
        /* 0x00E01FD4: dbf D2w */
        ;
    } while (scan_count-- != 0);

    /* 0x00E01FD8 */
    AST_$ASTE_SCAN_POS = aste;

    /*
     * 0x00E01FDC..0x00E02004: `lea (0x4,A6),A3` / `movea.l (-0xc,A3),A2`
     * reads A6-0x8 (best) first, `addq.l #0x4,A3` then A6-0x4 (second).
     * A success here goes straight to the counter update WITHOUT moving
     * the scan position.
     */
    cand = best;
    if (cand != NULL) {
        AST_$DEACTIVATE_SEGMENT(cand, 0, 0, &local_status);
        if (local_status == status_$ok) {
            aste = cand;
            goto count;
        }
    }
    cand = second;
    if (cand != NULL) {
        AST_$DEACTIVATE_SEGMENT(cand, 0, 0, &local_status);
        if (local_status == status_$ok) {
            aste = cand;
            goto count;
        }
    }

    /*
     * 0x00E02008..0x00E0205C: sweep 2 * AST_$SIZE_AST entries.  The count
     * is a 16-bit `add.w D0w,D0w / subq.w #1`; bmi skips to the crash.
     */
    aste = AST_$ASTE_SCAN_POS;
    scan_count = (int16_t)((int16_t)(AST_$SIZE_AST + AST_$SIZE_AST) - 1);
    if (scan_count >= 0) {
        do {
            aste = aste + 1;
            if (aste >= AST_$ASTE_LIMIT) {
                aste = ASTE_BASE;
            }

            /* 0x00E0202A..0x00E0203A: same second chance on bit 14 */
            if (aste->flags & ASTE_FLAG_LOCKED) {
                aste->flags &= (uint16_t)~ASTE_FLAG_LOCKED;
            } else {
                AST_$DEACTIVATE_SEGMENT(aste, 0, 0, &local_status);
                if (local_status == status_$ok) {
                    AST_$ALLOC_WORST_AST++;             /* 0x00E02052 */
                    goto found_scan;
                }
            }
            /* 0x00E0205C: dbf D2w */
        } while (scan_count-- != 0);
    }

    /*
     * 0x00E02060..0x00E0206C: nothing replaceable.  CRASH_SYSTEM does not
     * return; the image would otherwise go on to read the flags of a NULL
     * ASTE.
     */
    CRASH_SYSTEM(&ast_$no_replaceable_astes_00e01f18);
    aste = NULL;                                        /* suba.l A2,A2 */
    goto count;

found_scan:
    /* 0x00E02056 */
    AST_$ASTE_SCAN_POS = aste;

count:
    /* 0x00E0206E..0x00E0208A: btst.l #0xc (AREA), btst.l #0xb (remote) */
    flags = aste->flags;
    if (flags & ASTE_FLAG_AREA) {
        AST_$ASTE_AREA_CNT--;
    } else if (flags & ASTE_FLAG_REMOTE) {
        AST_$ASTE_R_CNT--;
    } else {
        AST_$ASTE_L_CNT--;
    }

done:
    /* 0x00E0208E..0x00E02092 */
    AST_$ALLOC_TOTAL_AST++;
    return aste;
}
