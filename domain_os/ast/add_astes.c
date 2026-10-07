/*
 * AST_$ADD_ASTES - Grow the ASTE pool by *count entries
 *
 * Each new ASTE (0x14 bytes) is carved off the top of the ASTE region at
 * AST_$ASTE_LIMIT and given the next segment index (AST_$SIZE_AST+1
 * upwards).  Its segment map, a 0x80-byte block of 32 page entries at
 * 0xED5000 + (seg-1)*0x80, is zeroed too; both pages are mapped on
 * demand through MMU_$VTOP / WP_$CALLOC / MMU_$INSTALL.  The entry is then
 * counted as a local ASTE and handed to AST_$FREE_ASTE.  Returns the new
 * AST_$SIZE_AST.
 *
 * Original address: 0x00E0118C (508 bytes), A5 = 0xE1DC80 (AST_ block):
 *   (0x400,A5) AST_$ASTE_LIMIT   (0x470,A5) AST_$SIZE_AST
 *   (0x476,A5) AST_$ASTE_L_CNT
 *
 * Frame: (0x8,A6) count word pointer, (0xC,A6) status pointer,
 *        (-0x10,A6) local status, (-0x14,A6) ppn, (-0x18,A6) va.
 */

#include "ast/ast_internal.h"
#include "misc/misc.h"
#include "mmu/mmu.h"

uint16_t AST_$ADD_ASTES(uint16_t *count, status_$t *status)
{
    status_$t local_status;     /* (-0x10,A6) */
    uint32_t ppn;               /* (-0x14,A6) */
    uint32_t va;                /* (-0x18,A6) */
    int16_t add_count;          /* D4 */
    int16_t remaining;          /* D3 */
    uint16_t seg;               /* D2: segment index of the entry */
    int32_t new_size;           /* D0 */
    aste_t *aste;               /* A2 */
    char *segmap_end;           /* D6: 0xED5000 + seg*0x80 */
    uint32_t segmap_va;         /* D5: segmap_end - 0x80 */
    uint16_t *clear_ptr;
    int16_t j;

    /* 0x00E0119A..0x00E011A0 */
    add_count = (int16_t)*count;
    local_status = status_$ok;

    /*
     * 0x00E011A4..0x00E011C0: zero-extended size plus sign-extended request
     * must land in [0x50, 0x1F8].
     */
    new_size = (int32_t)AST_$SIZE_AST + (int32_t)add_count;
    if (new_size > AST_MAX_ASTE || new_size < AST_MIN_ASTE) {
        /* 0x00E0136A */
        local_status = status_$ast_incompatible_request;
    } else {
        /*
         * 0x00E011C4..0x00E01212: map the page at the current limit if
         * MMU_$VTOP leaves a status; its return value is not examined.
         */
        va = ARCH_PTR_TO_VA(AST_$ASTE_LIMIT);
        (void)MMU_$VTOP(va, &local_status);
        if (local_status != status_$ok) {
            WP_$CALLOC(&ppn, &local_status);
            if (local_status != status_$ok) {
                CRASH_SYSTEM(&local_status);
            }
            MMU_$INSTALL(ppn, va, 0, 0x16);        /* pea (0x16).w */
        }

        /* 0x00E01216..0x00E01222 */
        ML_$LOCK(AST_LOCK_ID);

        /*
         * 0x00E01224..0x00E01248: D0 = size+1 (first new segment index),
         * D1 = size + count - D0 = count - 1 in 16 bits; bmi skips the loop.
         * D6 = 0xED5000 + (seg << 7) with seg zero-extended to 32 bits:
         * the END of segment seg's row (PMAP_SEGMAP_ROW(seg + 1)).
         */
        seg = (uint16_t)(AST_$SIZE_AST + 1);
        remaining = (int16_t)((int16_t)(AST_$SIZE_AST + add_count) -
                              (int16_t)seg);
        if (remaining >= 0) {
            segmap_end = (char *)PMAP_SEGMAP_ROW(seg + 1);
            do {
                /* 0x00E0124A..0x00E0125A: take the entry, bump the limit,
                 * va = address of the entry's last byte */
                aste = AST_$ASTE_LIMIT;
                AST_$ASTE_LIMIT = AST_$ASTE_LIMIT + 1;   /* moveq #0x14 / add.l */
                va = ARCH_PTR_TO_VA(AST_$ASTE_LIMIT) - 1;

                /* 0x00E0125E..0x00E0126A */
                ML_$UNLOCK(AST_LOCK_ID);

                /* 0x00E0126C..0x00E012B4: map the ASTE's page if needed */
                (void)MMU_$VTOP(va, &local_status);
                if (local_status != status_$ok) {
                    WP_$CALLOC(&ppn, &local_status);
                    if (local_status != status_$ok) {
                        CRASH_SYSTEM(&local_status);
                    }
                    MMU_$INSTALL(ppn, va, 0, 0x16);
                }

                /* 0x00E012B8..0x00E012C6: moveq #0x9 / dbf = 10 words, the
                 * whole 0x14-byte target entry */
                clear_ptr = (uint16_t *)aste;
                for (j = 0x9; j >= 0; j--) {
                    *clear_ptr++ = 0;
                }

                /* 0x00E012CA..0x00E01316: the segment map block for this
                 * segment starts 0x80 below D6; map its page if needed */
                segmap_va = ARCH_PTR_TO_VA(segmap_end - 0x80);
                (void)MMU_$VTOP(segmap_va, &local_status);
                if (local_status != status_$ok) {
                    WP_$CALLOC(&ppn, &local_status);
                    if (local_status != status_$ok) {
                        CRASH_SYSTEM(&local_status);
                    }
                    MMU_$INSTALL(ppn, segmap_va, 0, 0x16);
                }

                /* 0x00E0131A..0x00E0132A: moveq #0x3f / dbf = 0x40 words */
                clear_ptr = (uint16_t *)ARCH_VA_TO_PTR(segmap_va);
                for (j = 0x3F; j >= 0; j--) {
                    *clear_ptr++ = 0;
                }

                /* 0x00E0132E: move.w D2w,(0xe,A2) */
                aste->seg_index = seg;

                /* 0x00E01332..0x00E0134A */
                ML_$LOCK(AST_LOCK_ID);
                AST_$ASTE_L_CNT++;
                AST_$FREE_ASTE(aste);

                /* 0x00E0134C..0x00E01354: next segment, next map block */
                seg++;
                segmap_end += 0x80;
            } while (remaining-- != 0);
        }

        /* 0x00E01358..0x00E01368 */
        AST_$SIZE_AST += add_count;
        ML_$UNLOCK(AST_LOCK_ID);
    }

    /* 0x00E01372..0x00E0137A */
    *status = local_status;
    return AST_$SIZE_AST;
}
