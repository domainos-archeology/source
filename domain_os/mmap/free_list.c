/*
 * MMAP_$FREE_LIST - Return a next_vpn-linked chain of pages to the free
 * pool
 *
 * Original address: 0x00E0CB20 (100 bytes; `E0CB20 MMAP_$FREE_LIST` in the
 * SAU2 map).  No caller in the image (`gsk xrefs to 00e0cb20` is empty).
 *
 * Frame (0x00E0CB20-0x00E0CB2E): `link.w A6,#-0x10`, D2/D3/A2/A5 saved,
 * A5 = the MMAP_ block (0xE23284).  One argument: (0x8,A6) vpn_head,
 * longword (D2).
 *
 * 0x00E0CB32-0x00E0CB3C  token = ML_$SPIN_LOCK(&MMAP_GLOBALS.lock), kept in
 *                        (-0xA,A6)
 * 0x00E0CB40/0x00E0CB6A  loop head: D3 = D2; exit when zero
 * 0x00E0CB44-0x00E0CB54  A2 = 0xEB4800 + D3*0x10; D2 = page->next_vpn
 *                        (-0x1FF6 = +0xA, word zero-extended by `clr.l
 *                        D2') - read BEFORE the page is relinked
 * 0x00E0CB58-0x00E0CB66  mmap_$add_to_wsl(page, D3, 0, true)
 * 0x00E0CB6E-0x00E0CB76  ML_$SPIN_UNLOCK(&MMAP_GLOBALS.lock, token)
 */

#include "mmap/mmap_internal.h"

void MMAP_$FREE_LIST(uint32_t vpn_head)
{
    ml_$spin_token_t token;
    uint32_t next;   /* D2 */
    uint32_t vpn;    /* D3 */

    next = vpn_head;                                          /* 0x00E0CB2E */
    token = ML_$SPIN_LOCK(&MMAP_GLOBALS.lock);                /* 0x00E0CB32 */

    for (vpn = next; vpn != 0; vpn = next) {                  /* 0x00E0CB6A */
        mmape_t *page = MMAPE_FOR_VPN(vpn);                   /* 0x00E0CB44 */

        next = page->next_vpn;                                /* 0x00E0CB54 */
        mmap_$add_to_wsl(page, vpn, MMAP_WSL_POOL_FREE, true); /* 0x00E0CB58 */
    }

    ML_$SPIN_UNLOCK(&MMAP_GLOBALS.lock, token);               /* 0x00E0CB6E */
}
