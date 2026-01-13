/*
 * MMAP_$UNAVAIL_REMOV - Remove unavailable page from its WSL
 *
 * Simple wrapper that removes a page from its current working set list.
 *
 * Original address: 0x00e0cc30
 */

#include "mmap.h"

void MMAP_$UNAVAIL_REMOV(uint32_t vpn)
{
    mmape_t *page = MMAPE_FOR_VPN(vpn);
    mmap_$remove_from_wsl(page, vpn);
}
