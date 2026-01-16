/*
 * DBUF - Disk Buffer Management
 *
 * This module provides buffer caching for disk I/O operations.
 * It maintains an LRU (Least Recently Used) cache of disk blocks,
 * reducing disk access by keeping frequently-used blocks in memory.
 *
 * Key features:
 *   - Automatic buffer allocation based on available memory (6-64 buffers)
 *   - LRU replacement policy for buffer reclamation
 *   - Spin-lock protected for multiprocessor safety
 *   - Deferred write support for performance
 *   - Per-volume trouble tracking for error handling
 *
 * Buffer lifecycle:
 *   1. DBUF_$GET_BLOCK acquires a buffer (increments ref count)
 *   2. Caller reads/modifies buffer data
 *   3. DBUF_$SET_BUFF releases buffer (decrements ref count, optionally marks dirty)
 *   4. Dirty buffers are written back on demand or during UPDATE_VOL
 */

#ifndef DBUF_H
#define DBUF_H

#include "base/base.h"
#include "uid/uid.h"

/*
 * Buffer operation flags for DBUF_$SET_BUFF
 */
#define DBUF_FLAG_DIRTY         0x01    /* Mark buffer as dirty (needs writeback) */
#define DBUF_FLAG_WRITEBACK     0x02    /* Write back if dirty */
#define DBUF_FLAG_INVALIDATE    0x04    /* Invalidate buffer (discard contents) */
#define DBUF_FLAG_RELEASE       0x08    /* Decrement reference count */

/* Common flag combinations */
#define DBUF_RELEASE_CLEAN      0x08    /* Release without write */
#define DBUF_RELEASE_DIRTY      0x09    /* Mark dirty and release */
#define DBUF_RELEASE_WRITEBACK  0x0B    /* Write back immediately and release */

/*
 * DBUF_$INIT - Initialize the disk buffer subsystem
 *
 * Allocates and initializes the buffer pool based on available memory.
 * Number of buffers is calculated as: (real_pages / 1024) * 16
 * Clamped to range [6, 64].
 *
 * Must be called during system initialization before any disk I/O.
 *
 * Original address: 0x00e3abda
 */
void DBUF_$INIT(void);

/*
 * DBUF_$GET_BLOCK - Get a disk block into a buffer
 *
 * Retrieves a disk block into a memory buffer, reading from disk if necessary.
 * The buffer is locked until released with DBUF_$SET_BUFF.
 *
 * Uses LRU cache with spin lock protection. If the requested block is already
 * cached, returns the cached buffer. Otherwise allocates a buffer and reads
 * from disk. May block if all buffers are in use.
 *
 * Parameters:
 *   vol_idx     - Volume index (0-7)
 *   block       - Block number to read
 *   uid         - Pointer to expected UID for validation
 *   block_hint  - Block hint/type for allocation (encodes level info)
 *   flags       - Flags (bit 4: update UID from parameter, bit 5: allow stopped)
 *   status      - Receives status code
 *
 * Returns:
 *   Pointer to the buffer data (1024 bytes), or NULL on error.
 *   Buffer remains locked until DBUF_$SET_BUFF is called.
 *
 * Original address: 0x00e3a5b0
 */
void *DBUF_$GET_BLOCK(uint16_t vol_idx, int32_t block, uid_t *uid,
                      uint32_t block_hint, uint32_t flags,
                      status_$t *status);

/*
 * DBUF_$SET_BUFF - Release/update a disk buffer
 *
 * Releases a buffer obtained from DBUF_$GET_BLOCK and optionally marks
 * it for writeback. Must be called for every successful DBUF_$GET_BLOCK.
 *
 * Parameters:
 *   buffer - Pointer to the buffer data (from DBUF_$GET_BLOCK)
 *   flags  - Buffer operation flags (see DBUF_FLAG_* and common combinations)
 *   status - Receives status code
 *
 * Original address: 0x00e3a8b6
 */
void DBUF_$SET_BUFF(void *buffer, uint16_t flags, status_$t *status);

/*
 * DBUF_$INVALIDATE - Invalidate disk buffers
 *
 * Invalidates cached buffers for a specific block or all blocks on a volume.
 * Used when disk contents have changed externally or volume is being dismounted.
 *
 * Parameters:
 *   block   - Block number to invalidate (0 = all blocks on volume)
 *   vol_idx - Volume index (0-7)
 *
 * Also clears the DBUF_$TROUBLE flag for the volume.
 *
 * Original address: 0x00e3a9ec
 */
void DBUF_$INVALIDATE(int32_t block, uint16_t vol_idx);

/*
 * DBUF_$UPDATE_VOL - Flush dirty buffers for a volume
 *
 * Writes all dirty buffers for a specific volume (or all volumes) to disk.
 * Used during volume sync operations and before dismount.
 *
 * Parameters:
 *   vol_idx - Volume index to flush (0 = all volumes)
 *   uid_p   - Reserved (unused)
 *
 * Original address: 0x00e3aaa2
 */
void DBUF_$UPDATE_VOL(uint16_t vol_idx, void *uid_p);

#endif /* DBUF_H */
