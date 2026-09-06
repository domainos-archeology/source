/*
 * PROC2_$GET_BOOT_FLAGS - Get boot flags
 *
 * Returns the global boot flags value.
 *
 * Parameters:
 *   flags_ret - Pointer to receive boot flags
 *
 * Original address: 0x00e41b56
 */

#include "proc2/proc2_internal.h"

/* Global boot flags proc2_boot_flags at 0xe7c068 (= 0xe7be84 + 0x1e4),
 * declared in proc2_internal.h */

void PROC2_$GET_BOOT_FLAGS(int16_t *flags_ret)
{
    *flags_ret = proc2_boot_flags;
}
