/*
 * MMAP_$FREE_REMOVE - Pull a page out of whatever working set holds it and
 * return it to the free pool
 *
 * Original address: 0x00E0CB86 (98 bytes; `E0CB86 MMAP_$FREE_REMOVE` in the
 * SAU2 map).  Callers: 0x00E00F68, 0x00E0435E, 0x00E045E0, 0x00E05F54,
 * 0x00E06432.
 *
 * Frame (0x00E0CB86-0x00E0CB94): `link.w A6,#-4`, A2/A5 saved, A5 = the
 * MMAP_ block (0xE23284).  Arguments, (0x8,A6) being argument 1:
 *   (0x8,A6)  page  mmape_t pointer (A2)
 *   (0xC,A6)  vpn   longword
 *
 * 0x00E0CB98-0x00E0CBA8  if page->flags1 bit 7 (IN_WSL) is set (`tst.b /
 *                        bpl' skips): mmap_$remove_from_wsl(page, vpn) -
 *                        called BEFORE the spin lock is taken
 * 0x00E0CBAA-0x00E0CBB4  token = ML_$SPIN_LOCK(&MMAP_GLOBALS.lock)
 * 0x00E0CBB8             `bclr.b #7,(0x9,A2)': clear MMAPE_FLAG2_ON_DISK
 * 0x00E0CBBE-0x00E0CBCC  mmap_$add_to_wsl(page, vpn, 0, true)
 * 0x00E0CBD0-0x00E0CBD8  ML_$SPIN_UNLOCK(&MMAP_GLOBALS.lock, token)
 */

#include "mmap/mmap_internal.h"

void MMAP_$FREE_REMOVE(mmape_t *page, uint32_t vpn)
{
    ml_$spin_token_t token;

    if (page->flags1 & MMAPE_FLAG1_IN_WSL) {                 /* 0x00E0CB98 */
        mmap_$remove_from_wsl(page, vpn);                    /* 0x00E0CBA4 */
    }

    token = ML_$SPIN_LOCK(&MMAP_GLOBALS.lock);               /* 0x00E0CBAA */

    page->flags2 &= (uint8_t)~MMAPE_FLAG2_ON_DISK;           /* 0x00E0CBB8 */

    mmap_$add_to_wsl(page, vpn, MMAP_WSL_POOL_FREE, true);   /* 0x00E0CBBE */

    ML_$SPIN_UNLOCK(&MMAP_GLOBALS.lock, token);              /* 0x00E0CBD0 */
}
