/*
 * MMAP_$PURGE - Empty a working-set list
 *
 * Original address: 0x00E0D11C (60 bytes; `E0D11C MMAP_$PURGE` in the SAU2
 * map).  Callers: MMAP_$FREE_WSL (0x00E0D1A4) and 0x00E14746 area code;
 * MMAP_$FREE_WSL allocates a 2-byte Pascal result slot that nothing here
 * fills.
 *
 * Frame (0x00E0D11C-0x00E0D12A): `link.w A6,#0`, D2/A5 saved, A5 = the
 * MMAP_ block (0xE23284).  One argument: (0x8,A6) wsl_index, word (D2).
 *
 * 0x00E0D12E-0x00E0D13E  wsl_index > MMAP_$WSL_HI_MARK ((0xA22,A5), unsigned
 *                        `bls' skips) -> CRASH_SYSTEM(&mmap_$illegal_wsl_index
 *                        _00e0c9e0), the cell at 0x00E0C9E0 (00 06 00 09)
 *                        shared with SET_WS_PRI, SET_WS_INDEX and WS_SCAN
 * 0x00E0D140-0x00E0D14A  mmap_$trim_wsl(wsl_index, 0x3FFFFF) - with a
 *                        2-byte result slot the callee never fills
 */

#include "mmap/mmap_internal.h"
#include "misc/misc.h"

void MMAP_$PURGE(uint16_t wsl_index)
{
    if (wsl_index > MMAP_$WSL_HI_MARK) {                     /* 0x00E0D12E */
        CRASH_SYSTEM(&mmap_$illegal_wsl_index_00e0c9e0);     /* 0x00E0D138 */
    }

    mmap_$trim_wsl(wsl_index, MMAP_TRIM_PURGE_ALL);          /* 0x00E0D14A */
}
