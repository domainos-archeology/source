/*
 * MMAP_$FREE - Return one page to the free pool
 *
 * Original address: 0x00E0CAC2 (94 bytes; `E0CAC2 MMAP_$FREE` in the SAU2
 * map).  Eighteen callers (AST, PMAP, MST, NETWORK and the disk drivers).
 *
 * Frame (0x00E0CAC2-0x00E0CAD0): `link.w A6,#-8`, D2/A2/A5 saved, A5 = the
 * MMAP_ block (0xE23284).  One argument: (0x8,A6) vpn, longword (D2).
 *
 * 0x00E0CAD4-0x00E0CADE  A2 = 0xEB4800 + vpn*0x10 (mmape_t via -0x2000
 *                        displacements; -0x1FF7 = flags2, +9)
 * 0x00E0CAE2-0x00E0CAEC  token = ML_$SPIN_LOCK(&MMAP_GLOBALS.lock) - the
 *                        bare A5 base - kept in (-2,A6)
 * 0x00E0CAF0             `bclr.b #7' page->flags2: clear MMAPE_FLAG2_ON_DISK
 * 0x00E0CAF6-0x00E0CB04  mmap_$add_to_wsl(page, vpn, 0, true): `st -(SP)'
 *                        then `clr.w -(SP)' for the pool index
 * 0x00E0CB08-0x00E0CB10  ML_$SPIN_UNLOCK(&MMAP_GLOBALS.lock, token)
 */

#include "mmap/mmap_internal.h"

void MMAP_$FREE(uint32_t vpn)
{
    ml_$spin_token_t token;
    mmape_t *page = MMAPE_FOR_VPN(vpn);                      /* 0x00E0CAD4 */

    token = ML_$SPIN_LOCK(&MMAP_GLOBALS.lock);               /* 0x00E0CAE2 */

    page->flags2 &= (uint8_t)~MMAPE_FLAG2_ON_DISK;           /* 0x00E0CAF0 */

    mmap_$add_to_wsl(page, vpn, MMAP_WSL_POOL_FREE, true);   /* 0x00E0CAF6 */

    ML_$SPIN_UNLOCK(&MMAP_GLOBALS.lock, token);              /* 0x00E0CB08 */
}
