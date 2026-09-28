/*
 * MMAP_$RELEASE_PAGES - Drop a process's pages from its working set onto
 * the global pools
 *
 * Original address: 0x00E0CFF8 (292 bytes; `E0CFF8 MMAP_$RELEASE_PAGES` in
 * the SAU2 map).  Single caller: 0x00E07032.
 *
 * Frame (0x00E0CFF8-0x00E0D000): `link.w A6,#-0x14`, D2-D7/A2-A5 saved,
 * A5 = the MMAP_ block (0xE23284).  Arguments, (0x8,A6) being argument 1:
 *   (0x8,A6)  pid        word (D0)
 *   (0xA,A6)  vpn_array  longword array pointer
 *   (0xE,A6)  count      word (D1)
 *
 * 0x00E0D006-0x00E0D016  wsl_index (D2) = MMAP_PID_TO_WSL[pid]
 * 0x00E0D01A-0x00E0D01C  count == 0 skips everything
 * 0x00E0D040-0x00E0D10E  `dbf' on count-1 = count iterations, A4 = 0xEC5400
 *                        (the aste table) for the whole loop:
 *   0x00E0D04E-0x00E0D066  skip unless page->wsl_index == wsl_index, flags1
 *                          bit 7 (IN_WSL) set and wire_count == 0
 *   0x00E0D06A-0x00E0D074  mmap_$remove_from_wsl(page, vpn)
 *   0x00E0D076-0x00E0D090  dirty = `sne' of PFT second word bit 14
 *                          (MODIFIED) OR `sne' of flags2 bit 6 (MODIFIED)
 *   0x00E0D092-0x00E0D0A8  clean: pool 1 when flags1 bit 6 set, else 2
 *   0x00E0D0AA-0x00E0D0F2  dirty: with flags2 bit 7 (ON_DISK) set, `tst.w
 *                          (0x28,A0) / sne' on MMAP_$SEG_ASTE_FOR(segment)
 *                          ->aote (the high word of dtm_high); clear,
 *                          `tst.b (0xb9,A0) / smi' (remote_flag < 0); true
 *                          -> pool 4, false -> pool 3
 *   0x00E0D0F8-0x00E0D108  mmap_$add_to_wsl(page, vpn, pool, true)
 */

#include "mmap/mmap_internal.h"

void MMAP_$RELEASE_PAGES(uint16_t pid, uint32_t *vpn_array, uint16_t count)
{
    uint16_t wsl_index = MMAP_PID_TO_WSL[pid];               /* 0x00E0D016 */
    uint16_t i;

    if (count == 0) {                                        /* 0x00E0D01A */
        return;
    }

    for (i = 0; i < count; i++) {                            /* 0x00E0D040 */
        uint32_t vpn = vpn_array[i];
        mmape_t *page = MMAPE_FOR_VPN(vpn);
        uint16_t pool;                                       /* (-0x8,A6) */
        boolean dirty;

        if (page->wsl_index != wsl_index) {                  /* 0x00E0D054 */
            continue;
        }
        if (!(page->flags1 & MMAPE_FLAG1_IN_WSL)) {          /* 0x00E0D05A */
            continue;
        }
        if (page->wire_count != 0) {                         /* 0x00E0D062 */
            continue;
        }

        mmap_$remove_from_wsl(page, vpn);                    /* 0x00E0D070 */

        dirty = (PMAPE_FOR_VPN(vpn)[1] & PMAPE_FLAG_MODIFIED) ? true : false; /* 0x00E0D084 */
        if (page->flags2 & MMAPE_FLAG2_MODIFIED) {           /* 0x00E0D086 */
            dirty = true;
        }

        if (dirty >= 0) {                                    /* 0x00E0D090 */
            if (page->flags1 & MMAPE_FLAG1_IMPURE) {         /* 0x00E0D092 */
                pool = MMAP_WSL_POOL_PURE;                   /* 0x00E0D09A */
            } else {
                pool = MMAP_WSL_POOL_IMPURE;                 /* 0x00E0D0A2 */
            }
        } else {
            aote_t *aote = MMAP_$SEG_ASTE_FOR(page->segment)->aote;
            boolean needs_flush;

            if (page->flags2 & MMAPE_FLAG2_ON_DISK) {        /* 0x00E0D0AA */
                needs_flush = ((aote->dtm_high >> 16) != 0) ? true : false; /* 0x00E0D0C4 */
            } else {
                needs_flush = (aote->remote_flag < 0) ? true : false; /* 0x00E0D0E0 */
            }

            if (needs_flush < 0) {                           /* 0x00E0D0E6 */
                pool = MMAP_WSL_POOL_DIRTY_RMT;              /* 0x00E0D0EA */
            } else {
                pool = MMAP_WSL_POOL_DIRTY_LOCAL;            /* 0x00E0D0F2 */
            }
        }

        mmap_$add_to_wsl(page, vpn, pool, true);             /* 0x00E0D104 */
    }
}
