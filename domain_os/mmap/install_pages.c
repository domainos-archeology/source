/*
 * MMAP_$INSTALL_PAGES - Add freshly installed pages to a given process's
 * working set
 *
 * Original address: 0x00E0CDF0 (102 bytes; `E0CDF0 MMAP_$INSTALL_PAGES` in
 * the SAU2 map).  Single caller: 0x00E03F6C.
 *
 * Frame (0x00E0CDF0-0x00E0CDF8): `link.w A6,#-8`, D2/A2/A5 saved, A5 = the
 * MMAP_ block (0xE23284).  Arguments, (0x8,A6) being argument 1:
 *   (0x8,A6)  vpn_array  longword array pointer
 *   (0xC,A6)  count      word
 *   (0xE,A6)  pid        word
 *
 * 0x00E0CDFE-0x00E0CE0A  wsl_index (D2) = MMAP_PID_TO_WSL[pid]
 *                        ((0xA22,A5) + pid*2 = 0xE23CA6 + pid*2); no range
 *                        check on pid
 * 0x00E0CE0E-0x00E0CE1C  mmap_$add_pages_to_wsl(vpn_array, count, wsl_index)
 * 0x00E0CE1E-0x00E0CE34  A2 = A5 + index*0x24 (biased base: (0x30,A2) =
 *                        page_count, (0x3C,A2) = max_pages); page_count <=
 *                        max_pages (unsigned `bls') -> done
 * 0x00E0CE36-0x00E0CE44  mmap_$trim_wsl(wsl_index, page_count - max_pages)
 *                        with a 2-byte Pascal result slot that is never
 *                        read
 * 0x00E0CE48             MMAP_$WS_OVERFLOW++ (0x1C,A5)
 */

#include "mmap/mmap_internal.h"

void MMAP_$INSTALL_PAGES(uint32_t *vpn_array, uint16_t count, uint16_t pid)
{
    uint16_t wsl_index = MMAP_PID_TO_WSL[pid];                 /* 0x00E0CE0A */
    ws_hdr_t *wsl;

    mmap_$add_pages_to_wsl(vpn_array, count, wsl_index);       /* 0x00E0CE18 */

    wsl = &MMAP_$WSL[wsl_index];                               /* 0x00E0CE28 */
    if (wsl->page_count > wsl->max_pages) {                    /* 0x00E0CE2C */
        mmap_$trim_wsl(wsl_index, wsl->page_count - wsl->max_pages); /* 0x00E0CE44 */
        MMAP_$WS_OVERFLOW++;                                   /* 0x00E0CE48 */
    }
}
