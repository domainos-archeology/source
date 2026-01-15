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

/* Global boot flags at 0xe7c068 (= 0xe7be84 + 0x1e4) */
#if defined(M68K)
#define PROC2_BOOT_FLAGS (*(int16_t*)0xE7C068)
#else
#define PROC2_BOOT_FLAGS proc2_boot_flags
#endif

void PROC2_$GET_BOOT_FLAGS(int16_t *flags_ret)
{
    *flags_ret = PROC2_BOOT_FLAGS;
}
