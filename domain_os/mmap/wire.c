/*
 * MMAP_$WIRE - Add a wire to a page; a page sitting on one of the global
 * pools is pulled off it
 *
 * Original address: 0x00E0CCBC (90 bytes; `E0CCBC MMAP_$WIRE` in the SAU2
 * map).  No caller in the image (`gsk xrefs to 00e0ccbc` is empty).
 *
 * Frame (0x00E0CCBC-0x00E0CCCA): `link.w A6,#-4`, D2/A2/A5 saved, A5 = the
 * MMAP_ block (0xE23284).  One argument: (0x8,A6) vpn, longword (D2).
 *
 * 0x00E0CCCE-0x00E0CCD8  A2 = 0xEB4800 + vpn*0x10
 * 0x00E0CCDC-0x00E0CCEE  wire_count == 0x39 -> CRASH_SYSTEM(&mmap_$bad_
 *                        unavail_00e0cd18); the cell (00 06 00 06, "bad
 *                        unavail") is only used here
 * 0x00E0CCF0             `addq.b #1' wire_count
 * 0x00E0CCF4-0x00E0CD00  only when flags1 bit 7 (IN_WSL) is set AND
 *                        wsl_index < 5 (a global pool, unsigned `bcc'
 *                        skips):
 * 0x00E0CD02-0x00E0CD08  mmap_$remove_from_wsl(page, vpn)
 */

#include "mmap/mmap_internal.h"
#include "misc/misc.h"

/*
 * 0x00E0CCE4: pea (0x32,PC) -> 0x00E0CD18, jsr CRASH_SYSTEM at 0x00E0CCE8.
 * Image bytes 00 06 00 06 = status_$mmap_bad_unavail.
 */
#define status_$mmap_bad_unavail 0x00060006
static const status_$t mmap_$bad_unavail_00e0cd18 = status_$mmap_bad_unavail;

#define MMAP_MAX_WIRE_COUNT 0x39   /* cmpi.b #0x39 at 0x00E0CCDC */

void MMAP_$WIRE(uint32_t vpn)
{
    mmape_t *page = MMAPE_FOR_VPN(vpn);                      /* 0x00E0CCCE */

    if (page->wire_count == MMAP_MAX_WIRE_COUNT) {           /* 0x00E0CCDC */
        CRASH_SYSTEM(&mmap_$bad_unavail_00e0cd18);           /* 0x00E0CCE8 */
    }

    page->wire_count++;                                      /* 0x00E0CCF0 */

    if ((page->flags1 & MMAPE_FLAG1_IN_WSL)                  /* 0x00E0CCF4 */
        && page->wsl_index < WSL_INDEX_MIN_USER) {           /* 0x00E0CCFA */
        mmap_$remove_from_wsl(page, vpn);                    /* 0x00E0CD08 */
    }
}
