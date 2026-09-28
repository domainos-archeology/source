/*
 * MMAP_$SET_WS_MAX - Set a working-set list's page ceiling
 *
 * Original address: 0x00E0CA7A (72 bytes; `E0CA7A MMAP_$SET_WS_MAX` in the
 * SAU2 map).  Single caller: 0x00E5C736 (OSINFO).
 *
 * Frame (0x00E0CA7A-0x00E0CA82): `link.w A6,#0`, D2/D3/A5 saved, A5 = the
 * MMAP_ block (0xE23284).  Arguments, (0x8,A6) being argument 1:
 *   (0x8,A6)  wsl_index  word (D0)
 *   (0xA,A6)  max_pages  longword (D1)
 *   (0xE,A6)  status     longword pointer (out)
 *
 * 0x00E0CA90-0x00E0CA94  *status = 0
 * 0x00E0CA96-0x00E0CAA8  wsl_index < 5 (`bcs') or > 0x45 (`bls' skips):
 *                        *status = 0x60009 (status_$mmap_illegal_wsl_index)
 * 0x00E0CAAA-0x00E0CAB4  MMAP_$WSL[wsl_index].max_pages ((0x3C,A5) +
 *                        index*0x24 = +0x10) = max_pages
 */

#include "mmap/mmap_internal.h"

void MMAP_$SET_WS_MAX(uint16_t wsl_index, uint32_t max_pages,
                      status_$t *status)
{
    *status = status_$ok;                                    /* 0x00E0CA94 */

    if (wsl_index < WSL_INDEX_MIN_USER || wsl_index > WSL_INDEX_MAX) { /* 0x00E0CA96 */
        *status = status_$mmap_illegal_wsl_index;            /* 0x00E0CAA2 */
        return;
    }

    MMAP_$WSL[wsl_index].max_pages = max_pages;              /* 0x00E0CAB4 */
}
