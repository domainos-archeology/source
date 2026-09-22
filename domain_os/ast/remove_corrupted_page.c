/*
 * AST_$REMOVE_CORRUPTED_PAGE - Drop a frame whose contents are suspect
 *
 * Called from the parity/ECC path.  Nothing is done (FALSE returned)
 * when the AST or PMAP lock is held by the current process, the frame is
 * not a real page (outside 0x200..0xFFF), its MMAPE is not in a working
 * set or names segment 0, or its segment map entry is in transition or
 * not installed.  Otherwise: a page that is clean in both the PFT and
 * the MMAPE is invalidated with AST_$INVALIDATE_PAGE and TRUE returned;
 * a modified page cannot be dropped, so the owning object's UID is saved
 * with AST_$SAVE_CLOBBERED_UID (FALSE returned).
 *
 * Parameters (frame at 0x00E07276, `link.w A6,-0x14`):
 *   ppn (0x8,A6) longword (D3)
 * (-0x12,A6) holds the first lock test.  Returns D0b (D2b).
 *
 * Original address: 0x00E07276 (248 bytes), A5 = 0xE1DC80 (unused here).
 */

#include "ast/ast_internal.h"
#include "proc1/proc1.h"
#include "mmu/mmu.h"
#include "mmap/mmap.h"

uint8_t AST_$REMOVE_CORRUPTED_PAGE(uint32_t ppn)
{
    uint8_t result;             /* D2b */
    int8_t blocked;             /* D0b: TST_LOCK | TST_LOCK | scs */
    mmape_t *mmape;             /* A0 - 0x2000 */
    uint16_t seg;               /* D0w */
    aste_t *aste;               /* A2 - 0x14 */
    uint32_t *entry;            /* A3 - 0x80 */
    uint32_t *pft;

    /* 0x00E0728A */
    result = 0;

    /* 0x00E07288..0x00E072BA: either lock held, or ppn < 0x200 (scs) */
    blocked = (int8_t)(PROC1_$TST_LOCK(AST_LOCK_ID) |
                       PROC1_$TST_LOCK(PMAP_LOCK_ID) |
                       ((ppn < 0x200) ? 0xFF : 0x00));
    if (blocked < 0) {
        goto done;
    }
    /* 0x00E072BE */
    if (ppn > 0xFFF) {
        goto done;
    }

    /* 0x00E072C8..0x00E072E4: the MMAPE must be in a WSL (flags1 bit 7)
     * and name a segment */
    mmape = &MMAPE_BASE[ppn];
    seg = mmape->segment;
    if ((int8_t)mmape->flags1 >= 0) {
        goto done;
    }
    if (seg == 0) {
        goto done;
    }

    /* 0x00E072E6..0x00E072FA: 0xEC5400 + seg*0x14 = the ASTE after
     * ASTE_BASE[seg-1]; 0x00E072FE..0x00E07314: the map entry */
    aste = &ASTE_BASE[seg - 1];
    entry = (uint32_t *)((char *)SEGMAP_BASE + ((uint32_t)seg << 7) +
                         (uint16_t)(mmape->seg_offset << 2) - 0x80);

    /* 0x00E07318..0x00E07326: in transition, or not installed */
    if ((int32_t)*entry < 0) {
        goto done;
    }
    if ((*entry & SEGMAP_VALID) == 0) {
        goto done;
    }

    /* 0x00E07328..0x00E07342: PFT low word bit 14, MMAPE flags2 bit 6 */
    pft = PFT_FOR_PPN(ppn);
    if ((*pft & PFT_FLAG_MODIFIED) == 0 &&
        (mmape->flags2 & MMAPE_FLAG2_MODIFIED) == 0) {
        /* 0x00E07344..0x00E07352 */
        AST_$INVALIDATE_PAGE(aste, entry, ppn);
        result = 0xFF;
    } else {
        /* 0x00E07356..0x00E0735E: the object's UID at aote+0x10 */
        AST_$SAVE_CLOBBERED_UID(&aste->aote->uid);
    }

done:
    /* 0x00E07362 */
    return result;
}
