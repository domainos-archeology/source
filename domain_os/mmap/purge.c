/*
 * MMAP_$PURGE - Purge all pages from a working set list
 *
 * Removes all pages from the specified WSL by calling the
 * trim function with a special "purge all" value.
 *
 * Original address: 0x00e0d11c
 */

#include "mmap/mmap_internal.h"
#include "misc/misc.h"

/*
 * Status cells passed to CRASH_SYSTEM by `pea (d,PC)`.
 *
 * These are constant longwords in this module's own code region, not
 * shared globals; the cell address is part of each name.  Names come from
 * the SR10.4 status-code database.
 */
/*
 * 0x00E0D134: pea (-0x756,PC) -> 0x00E0C9E0, jsr CRASH_SYSTEM at 0x00E0D138.
 * Shared with MMAP_$SET_WS_PRI, MMAP_$WS_SCAN and MMAP_$SET_WS_INDEX.
 */
static const status_$t mmap_$illegal_wsl_index_00e0c9e0 = 0x00060009;

#define PURGE_ALL_MAGIC  0x3FFFFF

void MMAP_$PURGE(uint16_t wsl_index)
{
    if (wsl_index > MMAP_WSL_HI_MARK) {
        CRASH_SYSTEM(&mmap_$illegal_wsl_index_00e0c9e0);
    }

    mmap_$trim_wsl(wsl_index, PURGE_ALL_MAGIC);
}
