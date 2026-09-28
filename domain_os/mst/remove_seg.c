/*
 * MST_$REMOVE_SEG - Release the active-segment pages behind an MST entry
 *
 * Original address: 0x00E0E0D6 (SAU2 map: MST_WIRED, MST_$REMOVE_SEG at
 * E0E0D6; the batch listed 0x00E0E0EA, which is the third instruction)
 * Size: 72 bytes (0x00E0E0D6 .. 0x00E0E11D)
 *
 * Under ML lock 0x12 the routine looks the entry up in the AST with
 * AST_$LOCATE_ASTE and, if an ASTE exists, hands its pages back with
 * AST_$RELEASE_PAGES.
 *
 * Frame (link.w A6,-0x4; D2 saved):
 *   (0x8,A6)   request   pointer, the only one of the first four arguments
 *                        the body reads (`move.l (0x8,A6),-(SP)` 0x00E0E0EA)
 *   (0xc,A6)   param_2   longword, never read
 *   (0x10,A6)  param_3   word, never read
 *   (0x12,A6)  param_4   word, never read
 *   (0x14,A6)  flags     byte, read from the high half of its word slot
 *                        (`move.b (0x14,A6),-(SP)` 0x00E0E0FC)
 *
 * The one caller, MST_$UNMAP_PRIVI at 0x00E44A1A .. 0x00E44A34, pushes
 * `seq D1b` (a Domain boolean), D4w, D2w, D6 and `pea (-0x400,A2)` - the
 * MST entry itself, whose UID at +0 is what AST_$LOCATE_ASTE keys on.
 *
 * Verified against the disassembly 2026-09-19; the body was already faithful.
 */

#include "mst/mst_internal.h"

/*
 * @param request  the MST entry / locate request, BY REFERENCE
 * @param param_2  unused longword (A6+0x0C)
 * @param param_3  unused word (A6+0x10)
 * @param param_4  unused word (A6+0x12)
 * @param flags    Domain boolean forwarded to AST_$RELEASE_PAGES (A6+0x14)
 */
void MST_$REMOVE_SEG(locate_request_t *request, uint32_t param_2,
                     uint16_t param_3, uint16_t param_4, boolean flags)
{
    aste_t *aste;      /* A0 -> D2 */

    (void)param_2;
    (void)param_3;
    (void)param_4;

    /* 0x00E0E0DC .. 0x00E0E0E8: ML_$LOCK(0x12) */
    ML_$LOCK(MST_LOCK_AST);

    /* 0x00E0E0EA .. 0x00E0E0F8: aste = AST_$LOCATE_ASTE(request) (A0) */
    aste = AST_$LOCATE_ASTE(request);

    if (aste != NULL) {
        /* 0x00E0E0FA .. 0x00E0E108: AST_$RELEASE_PAGES(aste, flags), with
         * a Pascal result slot that nothing reads */
        AST_$RELEASE_PAGES(aste, flags);
    }

    /* 0x00E0E10A .. 0x00E0E116: ML_$UNLOCK(0x12) (no addq; unlk discards) */
    ML_$UNLOCK(MST_LOCK_AST);
}
