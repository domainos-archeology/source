/*
 * MMAP_$RECLAIM - Pull pages back off the global pools into the wired pool
 * or the current process's working set
 *
 * Original address: 0x00E0D914 (210 bytes; `E0D914 MMAP_$RECLAIM` in the
 * SAU2 map).  Callers: 0x00E031DA, 0x00E03600.
 *
 * Frame (0x00E0D914-0x00E0D91C): `link.w A6,#-0x14`, D2-D5/A2-A5 saved,
 * A5 = the MMAP_ block (0xE23284).  Arguments, (0x8,A6) being argument 1:
 *   (0x8,A6)  vpn_array  longword array pointer
 *   (0xC,A6)  count      word (D0)
 *   (0xE,A6)  use_wired  boolean, byte in the high half of its word slot
 *
 * 0x00E0D926-0x00E0D93C  wsl_index (D2) = 5 when use_wired < 0, else
 *                        MMAP_PID_TO_WSL[PROC1_$CURRENT]
 * 0x00E0D940-0x00E0D944  reclaimed (D1.b) = false; count == 0 skips the loop
 * 0x00E0D958-0x00E0D9A6  `dbf' on count-1 = count iterations:
 *   0x00E0D964-0x00E0D970  skip unless page->wsl_index <= 4 (one of the
 *                          global pools, unsigned `bhi') and flags1 bit 7
 *                          (IN_WSL) is set
 *   0x00E0D972-0x00E0D982  wsl_index == 0 (the free pool) ->
 *                          CRASH_SYSTEM(&mmap_$bad_reclaim_00e0d9e6)
 *   0x00E0D984-0x00E0D9A2  mmap_$remove_from_wsl(page, vpn);
 *                          mmap_$add_to_wsl(page, vpn, wsl_index, true);
 *                          reclaimed = true
 * 0x00E0D9AA-0x00E0D9D8  if anything was reclaimed and page_count >
 *                        max_pages ((0x30,A2) vs (0x3C,A2)):
 *                        mmap_$trim_wsl(wsl_index, page_count - max_pages)
 *                        (2-byte result slot never read) and
 *                        MMAP_$WS_OVERFLOW++
 */

#include "mmap/mmap_internal.h"
#include "misc/misc.h"
#include "proc1/proc1.h"

/*
 * 0x00E0D978: pea (0x6c,PC) -> 0x00E0D9E6, jsr CRASH_SYSTEM at 0x00E0D97C.
 * Image bytes 00 06 00 0d = "bad reclaim"; only this routine uses the cell.
 */
static const status_$t mmap_$bad_reclaim_00e0d9e6 = status_$mmap_bad_reclaim;

void MMAP_$RECLAIM(uint32_t *vpn_array, uint16_t count, boolean use_wired)
{
    uint16_t wsl_index;   /* D2 */
    boolean reclaimed;    /* D1.b */
    uint16_t i;

    if (use_wired < 0) {                                     /* 0x00E0D926 */
        wsl_index = MMAP_WSL_POOL_WIRED;                     /* 0x00E0D92C */
    } else {
        wsl_index = MMAP_PID_TO_WSL[PROC1_$CURRENT];         /* 0x00E0D930 */
    }

    reclaimed = false;                                       /* 0x00E0D940 */

    if (count != 0) {                                        /* 0x00E0D942 */
        for (i = 0; i < count; i++) {                        /* 0x00E0D958 */
            uint32_t vpn = vpn_array[i];
            mmape_t *page = MMAPE_FOR_VPN(vpn);

            if (page->wsl_index > MMAP_WSL_POOL_DIRTY_RMT) { /* 0x00E0D964 */
                continue;
            }
            if (!(page->flags1 & MMAPE_FLAG1_IN_WSL)) {      /* 0x00E0D96C */
                continue;
            }
            if (page->wsl_index == MMAP_WSL_POOL_FREE) {     /* 0x00E0D972 */
                CRASH_SYSTEM(&mmap_$bad_reclaim_00e0d9e6);   /* 0x00E0D97C */
            }

            mmap_$remove_from_wsl(page, vpn);                /* 0x00E0D98A */
            mmap_$add_to_wsl(page, vpn, wsl_index, true);    /* 0x00E0D99A */
            reclaimed = true;                                /* 0x00E0D9A2 */
        }
    }

    if (reclaimed < 0) {                                     /* 0x00E0D9AA */
        ws_hdr_t *wsl = &MMAP_$WSL[wsl_index];               /* 0x00E0D9AE */

        if (wsl->page_count > wsl->max_pages) {              /* 0x00E0D9BC */
            mmap_$trim_wsl(wsl_index, wsl->page_count - wsl->max_pages); /* 0x00E0D9D4 */
            MMAP_$WS_OVERFLOW++;                             /* 0x00E0D9D8 */
        }
    }
}
