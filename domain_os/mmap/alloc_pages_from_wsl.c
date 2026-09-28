/*
 * mmap_$alloc_pages_from_wsl - Take `count' pages off the head of a
 * working-set list
 *
 * Original address: 0x00E0D6F8 (150 bytes).  Module-local procedure of the
 * MMAP_ code segment (no map symbol); the only callers are MMAP_$ALLOC_PURE
 * (0x00E0D7E8) and MMAP_$ALLOC_FREE (0x00E0D8C2).  It does not set A5
 * itself: the `sub.l D0,(0xa14,A5)` at 0x00E0D780 relies on the caller's
 * A5 = 0xE23284, so (0xA14,A5) is MMAP_$PAGEABLE_PAGES (0xE23C98).
 *
 * Frame (0x00E0D6F8-0x00E0D704): `link.w A6,#-0x14`, D2-D4/A2 saved.
 * Arguments, (0x8,A6) being argument 1:
 *   (0x08,A6)  wsl        ws_hdr_t pointer (A0)
 *   (0x0C,A6)  vpn_array  longword array pointer (A1)
 *   (0x10,A6)  count      word (D1)
 *
 * The mmape_t array is addressed as 0xEB4800 + vpn*0x10 with -0x2000-based
 * displacements: -0x1FFB = flags1 (+5), -0x1FFA = prev_vpn (+6),
 * -0x1FF6 = next_vpn (+0xA).
 *
 * 0x00E0D708-0x00E0D70C  D2 = D3 = wsl->head_vpn (+0xC)
 * 0x00E0D70E-0x00E0D710  count == 0 skips the loop entirely
 * 0x00E0D712-0x00E0D73E  `dbf' on count-1 = count iterations: store D2 into
 *                        vpn_array[i], `bclr.b #7' the page's flags1 (clear
 *                        MMAPE_FLAG1_IN_WSL), D2 = page->prev_vpn (word,
 *                        zero-extended through `clr.l D4')
 * 0x00E0D742-0x00E0D74E  wsl->page_count -= count (zero-extended word)
 * 0x00E0D750-0x00E0D77C  if the count is still non-zero, relink: with
 *                        tail = mmape[first].next_vpn,
 *                        mmape[tail].prev_vpn = D2, mmape[D2].next_vpn =
 *                        tail, wsl->head_vpn = D2
 * 0x00E0D780            MMAP_$PAGEABLE_PAGES -= count
 *
 * When count is 0 and the list is non-empty, the relink block still runs
 * with D2 == first, which rewrites the existing links with their own
 * values; that is reproduced as found.
 */

#include "mmap/mmap_internal.h"

void mmap_$alloc_pages_from_wsl(ws_hdr_t *wsl, uint32_t *vpn_array,
                                uint16_t count)
{
    uint32_t first_vpn;    /* D3 */
    uint32_t current_vpn;  /* D2 */
    uint32_t n;            /* D0, zero-extended count */
    uint16_t i;

    first_vpn = wsl->head_vpn;                            /* 0x00E0D708 */
    current_vpn = first_vpn;

    if (count != 0) {                                      /* 0x00E0D70E */
        /* 0x00E0D71C-0x00E0D73E, count iterations */
        for (i = 0; i < count; i++) {
            mmape_t *page;

            vpn_array[i] = current_vpn;                    /* 0x00E0D71C */
            page = MMAPE_FOR_VPN(current_vpn);
            page->flags1 &= (uint8_t)~MMAPE_FLAG1_IN_WSL;  /* 0x00E0D72E */
            current_vpn = page->prev_vpn;                  /* 0x00E0D738 */
        }
    }

    n = count;                                             /* 0x00E0D742 */
    wsl->page_count -= n;                                  /* 0x00E0D74A */

    if (wsl->page_count != 0) {                            /* 0x00E0D74E */
        uint16_t tail;

        tail = MMAPE_FOR_VPN(first_vpn)->next_vpn;         /* 0x00E0D760 */
        MMAPE_FOR_VPN(tail)->prev_vpn = (uint16_t)current_vpn; /* 0x00E0D76C */
        MMAPE_FOR_VPN(current_vpn)->next_vpn = tail;       /* 0x00E0D778 */
        wsl->head_vpn = current_vpn;                       /* 0x00E0D77C */
    }

    MMAP_$PAGEABLE_PAGES -= n;                             /* 0x00E0D780 */
}
