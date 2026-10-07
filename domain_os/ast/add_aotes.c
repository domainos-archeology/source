/*
 * AST_$ADD_AOTES - Grow the AOTE pool by *count entries
 *
 * Each new AOTE (0xC0 bytes) is carved off the top of the AOTE region at
 * AST_$AOTE_LIMIT; if the page holding its last byte is not yet mapped
 * (MMU_$VTOP reports a status), a wired page is allocated with WP_$CALLOC
 * and installed there.  The entry is zeroed and handed to
 * ast_$release_aote, which puts it on the free list.  Returns the new
 * AST_$SIZE_AOT.
 *
 * Original address: 0x00E01014 (376 bytes), A5 = 0xE1DC80 (AST_ block):
 *   (0x3F4,A5) AST_$AOTE_LIMIT     (0x46E,A5) AST_$SIZE_AOT
 *
 * Frame: (0x8,A6) count word pointer, (0xC,A6) status pointer,
 *        (-0xC,A6) local status, (-0x10,A6) ppn, (-0x14,A6) va.
 */

#include "ast/ast_internal.h"
#include "misc/misc.h"
#include "mmu/mmu.h"

uint16_t AST_$ADD_AOTES(uint16_t *count, status_$t *status)
{
    status_$t local_status;     /* (-0xC,A6) */
    uint32_t ppn;               /* (-0x10,A6) */
    uint32_t va;                /* (-0x14,A6) */
    int16_t add_count;          /* D2 */
    int16_t remaining;          /* D3 */
    int32_t new_size;           /* D0 */
    aote_t *aote;               /* A2 */
    uint16_t *clear_ptr;
    int16_t j;

    /* 0x00E01022..0x00E01028 */
    add_count = (int16_t)*count;
    local_status = status_$ok;

    /*
     * 0x00E0102C..0x00E01048: clr.l D0 / move.w (0x46e,A5),D0w zero-extends
     * the current size; ext.l sign-extends the request.  Outside
     * [0x28, 0x118] the request is refused.
     */
    new_size = (int32_t)AST_$SIZE_AOT + (int32_t)add_count;
    if (new_size > AST_MAX_AOTE || new_size < AST_MIN_AOTE) {
        /* 0x00E0116E */
        local_status = status_$ast_incompatible_request;
    } else {
        /*
         * 0x00E0104C..0x00E0109A: make sure the page at the current limit
         * is mapped.  MMU_$VTOP's result is discarded; only the status it
         * writes (tst.l (-0xc,A6)) is examined.
         */
        va = ARCH_PTR_TO_VA(AST_$AOTE_LIMIT);
        (void)MMU_$VTOP(va, &local_status);
        if (local_status != status_$ok) {
            WP_$CALLOC(&ppn, &local_status);
            if (local_status != status_$ok) {
                CRASH_SYSTEM(&local_status);
            }
            MMU_$INSTALL(ppn, va, 0, 0x16);        /* pea (0x16).w */
        }

        /* 0x00E0109E..0x00E010AA */
        ML_$LOCK(AST_LOCK_ID);

        /*
         * 0x00E010AC..0x00E010BC: D1 = size + count - (size + 1), a 16-bit
         * computation that is count - 1; bmi skips the loop, else dbf runs
         * it count times.
         */
        remaining = (int16_t)((int16_t)(AST_$SIZE_AOT + add_count) -
                              (int16_t)(AST_$SIZE_AOT + 1));
        if (remaining >= 0) {
            do {
                /* 0x00E010C0..0x00E010D2: take the entry, bump the limit,
                 * va = address of the entry's last byte */
                aote = AST_$AOTE_LIMIT;
                AST_$AOTE_LIMIT = AST_$AOTE_LIMIT + 1;   /* addi.l #0xc0 */
                va = ARCH_PTR_TO_VA(AST_$AOTE_LIMIT) - 1;

                /* 0x00E010D6..0x00E010E2 */
                ML_$UNLOCK(AST_LOCK_ID);

                /* 0x00E010E4..0x00E0112C: same map-if-missing sequence */
                (void)MMU_$VTOP(va, &local_status);
                if (local_status != status_$ok) {
                    WP_$CALLOC(&ppn, &local_status);
                    if (local_status != status_$ok) {
                        CRASH_SYSTEM(&local_status);
                    }
                    MMU_$INSTALL(ppn, va, 0, 0x16);
                }

                /* 0x00E01130..0x00E0113E: moveq #0x5f / dbf = 0x60 words,
                 * the whole 0xC0-byte target entry */
                clear_ptr = (uint16_t *)aote;
                for (j = 0x5F; j >= 0; j--) {
                    *clear_ptr++ = 0;
                }

                /* 0x00E01142..0x00E01156 */
                ML_$LOCK(AST_LOCK_ID);
                ast_$release_aote(aote);

                /* 0x00E01158: dbf D3w */
            } while (remaining-- != 0);
        }

        /* 0x00E0115C..0x00E0116C */
        AST_$SIZE_AOT += add_count;
        ML_$UNLOCK(AST_LOCK_ID);
    }

    /* 0x00E01176..0x00E0117E */
    *status = local_status;
    return AST_$SIZE_AOT;
}
