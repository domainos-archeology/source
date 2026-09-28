/*
 * MMAP_$GET_WS_SIZ - Report a working-set list's page count and limits
 *
 * Original address: 0x00E0C9E4 (86 bytes; `E0C9E4 MMAP_$GET_WS_SIZ` in the
 * SAU2 map).  No caller in the image (`gsk xrefs to 00e0c9e4` is empty).
 *
 * Frame (0x00E0C9E4-0x00E0C9EC): `link.w A6,#-4`, D2/A2/A3/A5 saved, A5 =
 * the MMAP_ block (0xE23284).  Arguments, (0x8,A6) being argument 1:
 *   (0x08,A6)  wsl_index   word (D0)
 *   (0x0A,A6)  page_count  longword pointer (out)  <- ws_hdr_t +0x04
 *   (0x0E,A6)  field_14    longword pointer (out)  <- ws_hdr_t +0x14
 *   (0x12,A6)  max_pages   longword pointer (out)  <- ws_hdr_t +0x10
 *   (0x16,A6)  status      longword pointer (out)
 *
 * 0x00E0C9F6-0x00E0C9FA  *status = 0
 * 0x00E0C9FC-0x00E0CA08  wsl_index > 0x45 (unsigned `bls' skips):
 *                        *status = 0x60009 (status_$mmap_illegal_wsl_index),
 *                        done - the three outputs are left untouched
 * 0x00E0CA0A-0x00E0CA14  A0 = A5 + index*0x24 (built as *4 + *32), the
 *                        biased base: (0x30,A0) = page_count, (0x40,A0) =
 *                        field_14, (0x3C,A0) = max_pages
 * 0x00E0CA18-0x00E0CA2C  the three copies, in that order
 */

#include "mmap/mmap_internal.h"

void MMAP_$GET_WS_SIZ(uint16_t wsl_index, uint32_t *page_count,
                      uint32_t *field_14, uint32_t *max_pages,
                      status_$t *status)
{
    ws_hdr_t *wsl;

    *status = status_$ok;                                    /* 0x00E0C9FA */

    if (wsl_index > WSL_INDEX_MAX) {                         /* 0x00E0C9FC */
        *status = status_$mmap_illegal_wsl_index;            /* 0x00E0CA02 */
        return;
    }

    wsl = &MMAP_$WSL[wsl_index];                             /* 0x00E0CA0A */
    *page_count = wsl->page_count;                           /* 0x00E0CA1C */
    *field_14 = wsl->field_14;                               /* 0x00E0CA24 */
    *max_pages = wsl->max_pages;                             /* 0x00E0CA2C */
}
