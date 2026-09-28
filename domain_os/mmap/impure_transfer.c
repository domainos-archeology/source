/*
 * MMAP_$IMPURE_TRANSFER - Move a page off one of the two dirty pools onto
 * the pure pool
 *
 * Original address: 0x00E0CBE8 (72 bytes; `E0CBE8 MMAP_$IMPURE_TRANSFER` in
 * the SAU2 map).  Single caller: 0x00E065BA.
 *
 * Frame (0x00E0CBE8-0x00E0CBF6): `link.w A6,#0`, A2/A5 saved, A5 = the
 * MMAP_ block (0xE23284).  Arguments, (0x8,A6) being argument 1:
 *   (0x8,A6)  page  mmape_t pointer (A2)
 *   (0xC,A6)  vpn   longword
 *
 * 0x00E0CBFA-0x00E0CC08  page->wsl_index (byte, +4) must be 4
 *                        (MMAP_WSL_POOL_DIRTY_RMT) or 3
 *                        (MMAP_WSL_POOL_DIRTY_LOCAL); anything else
 *                        returns without touching the page
 * 0x00E0CC0A-0x00E0CC14  mmap_$remove_from_wsl(page, vpn)
 * 0x00E0CC16-0x00E0CC22  mmap_$add_to_wsl(page, vpn, 1, false): the single
 *                        `move.l #0x10000,-(SP)' is the word index 1
 *                        (MMAP_WSL_POOL_PURE) over the zero boolean slot
 *
 * No spin lock is taken here.
 */

#include "mmap/mmap_internal.h"

void MMAP_$IMPURE_TRANSFER(mmape_t *page, uint32_t vpn)
{
    uint8_t pool = page->wsl_index;                          /* 0x00E0CBFA */

    if (pool == MMAP_WSL_POOL_DIRTY_RMT                      /* 0x00E0CBFE */
        || pool == MMAP_WSL_POOL_DIRTY_LOCAL) {              /* 0x00E0CC04 */
        mmap_$remove_from_wsl(page, vpn);                    /* 0x00E0CC10 */
        mmap_$add_to_wsl(page, vpn, MMAP_WSL_POOL_PURE, false); /* 0x00E0CC22 */
    }
}
