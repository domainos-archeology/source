/*
 * CACHE - Cache Management Module
 *
 * This module provides cache management operations for Domain/OS.
 * On systems with cached memory, these functions ensure data coherency
 * between CPU caches and main memory.
 */

#ifndef CACHE_H
#define CACHE_H

#include "base/base.h"

/*
 * CACHE_$CLEAR - Clear/invalidate CPU instruction cache
 *
 * Clears the CPU instruction cache by setting bit 3 (CI - Clear Instruction
 * cache) in the Cache Control Register (CACR). This ensures that any
 * modified code in memory will be fetched fresh from RAM.
 *
 * On 68020/030: Sets the CI bit in CACR to invalidate the instruction cache.
 * On 68010: No instruction cache exists, but this function exists for
 *           compatibility with later processors.
 *
 * Returns: The new CACR value with bit 3 set
 *
 * Original address: 0x00E242D4
 * Size: 14 bytes
 */
uint32_t CACHE_$CLEAR(void);

/*
 * CACHE_$FLUSH_VIRTUAL - Flush virtual address cache
 *
 * Flushes the CPU data cache for virtual addresses to ensure
 * modifications are visible to DMA and other bus masters.
 *
 * On SAU2 (68010-based systems): This is a no-op since the 68010 has no
 * data cache. The function exists for API compatibility with later
 * processors that do have data caches.
 *
 * Original address: 0x00E242E2
 * Size: 2 bytes
 */
void CACHE_$FLUSH_VIRTUAL(void);

#endif /* CACHE_H */
