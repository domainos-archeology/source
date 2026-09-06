/*
 * MMAP_$UNAVAIL_REMOV - Remove unavailable page from its WSL
 *
 * Simple wrapper that removes a page from its current working set list.
 *
 * Pascal signature recovered from 0x00E0CC30:
 *   procedure mmap_$unavail_remov(vpn: integer32; flag: boolean);
 * The frame only reads (0x8,A6) - the VPN.  The boolean at (0xC,A6) is
 * pushed as TRUE (`st -(SP)`) by all three call sites (0x00E13884,
 * 0x00E13D60, 0x00E14358) and is never examined by the callee; it is kept
 * in the C signature so the stack shape matches the original.
 *
 * Original address: 0x00e0cc30
 */

#include "mmap/mmap_internal.h"

void MMAP_$UNAVAIL_REMOV(uint32_t vpn, boolean unused_flag)
{
    (void)unused_flag;  /* 0x00E0CC30 never reads (0xC,A6) */

    mmape_t *page = MMAPE_FOR_VPN(vpn);
    mmap_$remove_from_wsl(page, vpn);
}
