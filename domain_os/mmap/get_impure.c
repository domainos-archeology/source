/*
 * MMAP_$GET_IMPURE - Pull dirty pages off a working-set list for the
 * purifiers
 *
 * Original address: 0x00E0D5EA (270 bytes; `E0D5EA MMAP_$GET_IMPURE` in the
 * SAU2 map).  Callers: PMAP_$PURIFIER_L (0x00E13BB6) and PMAP_$PURIFIER_R
 * (0x00E1428C).
 *
 * Frame (0x00E0D5EA-0x00E0D5F2): `link.w A6,#-0x24`, D2-D7/A2-A5 saved,
 * A5 = the MMAP_ block (0xE23284).  Arguments, (0x8,A6) being argument 1:
 *   (0x08,A6)  wsl_index  word
 *   (0x0A,A6)  vpn_array  longword array pointer (A1, post-incremented)
 *   (0x0E,A6)  all_pages  boolean, byte in the high half of its word slot
 *   (0x10,A6)  max_pages  word
 *   (0x12,A6)  scanned    longword pointer (out)
 *   (0x16,A6)  returned   word pointer (out)
 *
 * Registers: D1 = the page being examined, D2 = the next one
 * (page->prev_vpn), D3 = pages returned, D4 = pages scanned, D5.b = "take
 * this page", A0 = the WSL header via the biased A5 base ((0x30,A0) =
 * page_count, (0x38,A0) = head_vpn), A2 = 0xEB4800 for the -0x2000-based
 * mmape_t displacements (-0x1FFE segment, -0x1FFB flags1, -0x1FFA
 * prev_vpn, -0x1FF7 flags2, -0x1FF6 next_vpn).
 *
 * 0x00E0D5F8-0x00E0D60E  wsl = &MMAP_$WSL[wsl_index] (index * 0x24 as
 *                        *4 + *32); D1 = wsl->head_vpn
 * 0x00E0D612-0x00E0D630  scanned = 0, returned = 0; max_scan (-0x14,A6) =
 *                        page_count when all_pages < 0, otherwise
 *                        min(100, page_count) (`moveq #0x64' + `cmp.l /
 *                        bls')
 * 0x00E0D640-0x00E0D64C  loop while returned < max_pages (unsigned `bcc')
 *                        and scanned < max_scan
 * 0x00E0D64E-0x00E0D660  page = mmape[D1]; D0 = D2 = page->prev_vpn;
 *                        D5.b = all_pages - the "take it" flag STARTS as
 *                        the all_pages argument (`move.b (0xe,A6),D5b' at
 *                        0x00E0D654)
 * 0x00E0D662-0x00E0D68C  if flags2 bit 7 (ON_DISK) is clear: read the
 *                        owning object's attribute word
 *                        (MMAP_$SEG_ASTE_FOR(page->segment)->aote + 0x0E,
 *                        the 0xEC5400 table with a 0x14 stride and the
 *                        aote pointer at -0x10) and `st D5b' when bit 12
 *                        is set
 * 0x00E0D68E-0x00E0D6BA  if D5 < 0: returned++, *vpn_array++ = D1, then
 *                        unlink - mmape[prev_vpn].next_vpn =
 *                        page->next_vpn, mmape[next_vpn].prev_vpn =
 *                        prev_vpn - and `bclr.b #7' flags1 (IN_WSL),
 *                        `bclr.b #6' flags2 (MODIFIED)
 * 0x00E0D6C0-0x00E0D6C4  scanned++; D1 = D2
 * 0x00E0D6C8-0x00E0D6DA  wsl->page_count -= returned; if still non-zero,
 *                        wsl->head_vpn = D1
 * 0x00E0D6DE-0x00E0D6E8  *returned = D3; *scanned = D4
 * 0x00E0D6EA             MMAP_$PAGEABLE_PAGES (0xA14,A5) -= returned
 *
 * No spin lock is taken here.
 */

#include "mmap/mmap_internal.h"

void MMAP_$GET_IMPURE(uint16_t wsl_index, uint32_t *vpn_array,
                      boolean all_pages, uint16_t max_pages,
                      uint32_t *scanned, uint16_t *returned)
{
    ws_hdr_t *wsl = &MMAP_$WSL[wsl_index];                      /* 0x00E0D5F8 */
    uint32_t vpn;          /* D1 */
    uint32_t next;         /* D2 */
    uint16_t n_returned;   /* D3 */
    uint32_t n_scanned;    /* D4 */
    boolean take;          /* D5.b */
    uint32_t max_scan;     /* (-0x14,A6) */
    uint32_t n;            /* D0 at 0x00E0D6C8 */

    vpn = wsl->head_vpn;                                        /* 0x00E0D60E */
    n_scanned = 0;                                              /* 0x00E0D612 */
    n_returned = 0;                                             /* 0x00E0D614 */

    if (all_pages < 0) {                                        /* 0x00E0D616 */
        max_scan = wsl->page_count;                             /* 0x00E0D61C */
    } else {
        max_scan = 100;                                         /* 0x00E0D624 */
        if (max_scan > wsl->page_count) {                       /* 0x00E0D626 */
            max_scan = wsl->page_count;
        }
    }

    /* 0x00E0D640-0x00E0D6C4 */
    while (n_returned < max_pages && n_scanned < max_scan) {
        mmape_t *page = MMAPE_FOR_VPN(vpn);                     /* 0x00E0D658 */
        uint16_t prev_vpn;

        take = all_pages;                                       /* 0x00E0D654 */
        prev_vpn = page->prev_vpn;                              /* 0x00E0D65C */
        next = prev_vpn;                                        /* 0x00E0D660 */

        if (!(page->flags2 & MMAPE_FLAG2_ON_DISK)) {            /* 0x00E0D662 */
            aote_t *aote = MMAP_$SEG_ASTE_FOR(page->segment)->aote; /* 0x00E0D67E */

            if (MMAP_AOTE_ATTR_FLAGS(aote) & MMAP_AOTE_ATTR_FLAG_BIT12) { /* 0x00E0D686 */
                take = true;                                    /* 0x00E0D68C */
            }
        }

        if (take < 0) {                                         /* 0x00E0D68E */
            uint16_t next_vpn;

            n_returned++;                                       /* 0x00E0D692 */
            *vpn_array++ = vpn;                                 /* 0x00E0D694 */

            next_vpn = page->next_vpn;                          /* 0x00E0D69A */
            MMAPE_FOR_VPN(prev_vpn)->next_vpn = next_vpn;       /* 0x00E0D6A4 */
            MMAPE_FOR_VPN(next_vpn)->prev_vpn = prev_vpn;       /* 0x00E0D6B0 */

            page->flags1 &= (uint8_t)~MMAPE_FLAG1_IN_WSL;       /* 0x00E0D6B4 */
            page->flags2 &= (uint8_t)~MMAPE_FLAG2_MODIFIED;     /* 0x00E0D6BA */
        }

        n_scanned++;                                            /* 0x00E0D6C0 */
        vpn = next;                                             /* 0x00E0D6C2 */
    }

    n = n_returned;                                             /* 0x00E0D6C8 */
    wsl->page_count -= n;                                       /* 0x00E0D6D0 */
    if (wsl->page_count != 0) {                                 /* 0x00E0D6D4 */
        wsl->head_vpn = vpn;                                    /* 0x00E0D6DA */
    }

    *returned = n_returned;                                     /* 0x00E0D6E2 */
    *scanned = n_scanned;                                       /* 0x00E0D6E8 */

    MMAP_$PAGEABLE_PAGES -= n;                                  /* 0x00E0D6EA */
}
