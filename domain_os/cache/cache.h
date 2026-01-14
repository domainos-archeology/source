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
 * CACHE_$FLUSH_VIRTUAL - Flush virtual address cache
 *
 * Flushes the CPU data cache for virtual addresses to ensure
 * modifications are visible to DMA and other processors.
 *
 * On systems without data caches, this is a no-op.
 *
 * Original address: TBD
 */
void CACHE_$FLUSH_VIRTUAL(void);

/*
 * CACHE_$CLEAR - Clear cache entries
 *
 * Clears/invalidates cache entries.
 *
 * Original address: TBD
 */
void CACHE_$CLEAR(void);

#endif /* CACHE_H */
