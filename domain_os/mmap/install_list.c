/*
 * MMAP_$INSTALL_LIST - Add freshly installed pages to the wired pool or to
 * the current process's working set
 *
 * Original address: 0x00E0CD80 (112 bytes; `E0CD80 MMAP_$INSTALL_LIST` in
 * the SAU2 map).  Callers: 0x00E034C4, 0x00E039B2, 0x00E0440E, 0x00E04676.
 *
 * Frame (0x00E0CD80-0x00E0CD88): `link.w A6,#-8`, D2/A2/A5 saved, A5 = the
 * MMAP_ block (0xE23284).  Arguments, (0x8,A6) being argument 1:
 *   (0x8,A6)  vpn_array  longword array pointer
 *   (0xC,A6)  count      word
 *   (0xE,A6)  use_wired  boolean, byte in the high half of its word slot
 *
 * 0x00E0CD8E-0x00E0CDA4  wsl_index (D2) = 5 (the wired pool) when
 *                        use_wired < 0, else MMAP_PID_TO_WSL[PROC1_$CURRENT]
 *                        ((0xA22,A5) + pid*2)
 * 0x00E0CDA8-0x00E0CDB6  mmap_$add_pages_to_wsl(vpn_array, count, wsl_index)
 * 0x00E0CDB8-0x00E0CDCE  A2 = A5 + index*0x24 (biased base: (0x30,A2) =
 *                        page_count, (0x3C,A2) = max_pages); page_count <=
 *                        max_pages (unsigned `bls') -> done
 * 0x00E0CDD0-0x00E0CDDE  mmap_$trim_wsl(wsl_index, page_count - max_pages)
 *                        with a 2-byte Pascal result slot that is never
 *                        read
 * 0x00E0CDE2             MMAP_$WS_OVERFLOW++ (0x1C,A5)
 */

#include "mmap/mmap_internal.h"
#include "proc1/proc1.h"

void MMAP_$INSTALL_LIST(uint32_t *vpn_array, uint16_t count, boolean use_wired)
{
    uint16_t wsl_index;   /* D2 */
    ws_hdr_t *wsl;

    if (use_wired < 0) {                                       /* 0x00E0CD8E */
        wsl_index = MMAP_WSL_POOL_WIRED;                       /* 0x00E0CD94 */
    } else {
        wsl_index = MMAP_PID_TO_WSL[PROC1_$CURRENT];           /* 0x00E0CD98 */
    }

    mmap_$add_pages_to_wsl(vpn_array, count, wsl_index);       /* 0x00E0CDB2 */

    wsl = &MMAP_$WSL[wsl_index];                               /* 0x00E0CDB8 */
    if (wsl->page_count > wsl->max_pages) {                    /* 0x00E0CDC6 */
        mmap_$trim_wsl(wsl_index, wsl->page_count - wsl->max_pages); /* 0x00E0CDDE */
        MMAP_$WS_OVERFLOW++;                                   /* 0x00E0CDE2 */
    }
}
