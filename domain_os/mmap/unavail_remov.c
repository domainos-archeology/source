/*
 * MMAP_$UNAVAIL_REMOV - Unlink a page from its working-set list
 *
 * Original address: 0x00E0CC30 (52 bytes; `E0CC30 MMAP_$UNAVAIL_REMOV` in
 * the SAU2 map).  Callers: 0x00E13884, 0x00E13D60, 0x00E14358 (the PMAP
 * purifiers), each pushing `st -(SP)` after the VPN.
 *
 * Frame (0x00E0CC30-0x00E0CC3E): `link.w A6,#-4`, D2/A2/A5 saved, A5 = the
 * MMAP_ block (0xE23284).  Arguments, (0x8,A6) being argument 1:
 *   (0x8,A6)  vpn          longword (D2)
 *   (0xC,A6)  unused_flag  boolean, pushed TRUE by every caller and never
 *                          read here; kept so the stack shape matches
 *
 * 0x00E0CC42-0x00E0CC4C  A2 = 0xEB4800 + vpn*0x10
 * 0x00E0CC50-0x00E0CC56  mmap_$remove_from_wsl(page, vpn)
 */

#include "mmap/mmap_internal.h"

void MMAP_$UNAVAIL_REMOV(uint32_t vpn, boolean unused_flag)
{
    (void)unused_flag; /* (0xC,A6) is never read */

    mmap_$remove_from_wsl(MMAPE_FOR_VPN(vpn), vpn);          /* 0x00E0CC56 */
}
