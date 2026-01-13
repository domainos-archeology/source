/*
 * MMAP_$PURGE - Purge all pages from a working set list
 *
 * Removes all pages from the specified WSL by calling the
 * trim function with a special "purge all" value.
 *
 * Original address: 0x00e0d11c
 */

#include "mmap.h"

#define PURGE_ALL_MAGIC  0x3FFFFF

void MMAP_$PURGE(uint16_t wsl_index)
{
    if (wsl_index > MMAP_WSL_HI_MARK) {
        CRASH_SYSTEM(Illegal_WSL_Index_Err);
    }

    mmap_$trim_wsl(wsl_index, PURGE_ALL_MAGIC);
}
