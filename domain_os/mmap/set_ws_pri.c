/*
 * MMAP_$SET_WS_PRI - Stamp the current process's working set with the clock
 *
 * Original address: 0x00E0C98A (86 bytes; `E0C98A MMAP_$SET_WS_PRI`, the
 * first map symbol of the MMAP_ code segment).  No caller in the image
 * (`gsk xrefs to 00e0c98a` is empty).
 *
 * Frame (0x00E0C98A-0x00E0C992): `link.w A6,#-4`, D2/A5 saved, A5 = the
 * MMAP_ block (0xE23284).  No arguments.
 *
 * 0x00E0C998-0x00E0C9A4  wsl_index = MMAP_PID_TO_WSL[PROC1_$CURRENT]
 * 0x00E0C9A8-0x00E0C9B2  in range when wsl_index <= 0x45 (`bhi' out) and
 *                        >= 5 (`bcc' in)
 * 0x00E0C9B4-0x00E0C9C2  out of range: 0 is silently ignored, anything
 *                        else crashes with the 0x00E0C9E0 cell (shared
 *                        with PURGE, SET_WS_INDEX and WS_SCAN) and skips
 *                        the store
 * 0x00E0C9C4-0x00E0C9CE  MMAP_$WSL[wsl_index].pri_timestamp ((0x44,A5) +
 *                        index*0x24 = +0x18) = TIME_$CLOCKH (0xE2B0D4)
 */

#include "mmap/mmap_internal.h"
#include "misc/misc.h"
#include "proc1/proc1.h"
#include "time/time.h"

void MMAP_$SET_WS_PRI(void)
{
    uint16_t wsl_index = MMAP_PID_TO_WSL[PROC1_$CURRENT];    /* 0x00E0C998 */

    if (wsl_index > WSL_INDEX_MAX || wsl_index < WSL_INDEX_MIN_USER) { /* 0x00E0C9A8 */
        if (wsl_index != 0) {                                /* 0x00E0C9B4 */
            CRASH_SYSTEM(&mmap_$illegal_wsl_index_00e0c9e0); /* 0x00E0C9BC */
        }
        return;
    }

    MMAP_$WSL[wsl_index].pri_timestamp = TIME_$CLOCKH;       /* 0x00E0C9CE */
}
