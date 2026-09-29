/*
 * AST_$INVALIDATE_PAGE - Drop one installed page from a segment map entry
 *
 * If the entry says the page is wired in the MMU (bit 29) that mapping
 * is removed; then the installed bit (30) is cleared, the entry's low 23
 * bits are replaced by the MMAPE's disk address, the frame is given back
 * with MMAP_$FREE_REMOVE and the ASTE's page count goes down.
 *
 * Parameters (frame at 0x00E00F16, `link.w A6,-0x4`):
 *   aste          (0x8,A6)  (A2)
 *   segmap_entry  (0xC,A6)  (A1)
 *   ppn           (0x10,A6) longword (D2); its MMAPE is 0xEB4800 + ppn*16
 *                 addressed through the -0x2000 bias
 *
 * Original address: 0x00E00F16 (102 bytes).  No A5.
 */

#include "ast/ast_internal.h"
#include "mmu/mmu.h"
#include "mmap/mmap.h"

void AST_$INVALIDATE_PAGE(aste_t *aste, uint32_t *segmap_entry, uint32_t ppn)
{
    mmape_t *mmape;             /* A3 - 0x2000 */

    /* 0x00E00F26..0x00E00F30 */
    mmape = MMAPE_FOR_VPN(ppn);

    /* 0x00E00F34..0x00E00F4C: btst.l #0xd on the high word = bit 29;
     * bclr.b #0x5,(A1) clears it */
    if (*segmap_entry & SEGMAP_WIRED) {
        *segmap_entry &= ~SEGMAP_WIRED;
        MMU_$REMOVE(ppn);
    }

    /* 0x00E00F4E..0x00E00F60: bclr.b #0x6 = bit 30; keep the top nine
     * bits, take the MMAPE's disk address */
    *segmap_entry &= ~SEGMAP_VALID;
    *segmap_entry &= 0xFF800000u;
    *segmap_entry |= mmape->disk_addr;

    /* 0x00E00F62..0x00E00F68 */
    MMAP_$FREE_REMOVE(mmape, ppn);

    /* 0x00E00F6E: subq.b #0x1,(0x10,A2) */
    aste->page_count--;
}
