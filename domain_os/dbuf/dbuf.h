/*
 * DBUF - Disk Buffer Management
 *
 * This module provides buffer management for disk I/O operations.
 * Buffers are cached in memory and managed for efficient disk access.
 */

#ifndef DBUF_H
#define DBUF_H

#include "base/base.h"

/*
 * DBUF_$GET_BLOCK - Get a disk block into a buffer
 *
 * Retrieves a disk block into a memory buffer, reading from disk if necessary.
 * The buffer is locked until released with DBUF_$SET_BUFF.
 *
 * Uses a buffer cache with LRU replacement. If the requested block is already
 * cached, returns the cached buffer. Otherwise allocates a buffer and reads
 * from disk.
 *
 * Parameters:
 *   vol_idx     - Volume index (0-7)
 *   block       - Block number to read
 *   uid         - Pointer to expected UID for validation
 *   block_hint  - Block hint/type for allocation (encodes level info)
 *   flags       - Flags (high word: 0=VTOC read, 1=file data read)
 *   status      - Receives status code
 *
 * Returns:
 *   Pointer to the buffer containing the block data, or NULL on error
 *
 * Original address: 0x00e3a5b0
 */
extern void *DBUF_$GET_BLOCK(ushort vol_idx, int block, void *uid,
                             uint32_t block_hint, uint32_t flags,
                             status_$t *status);

/*
 * DBUF_$SET_BUFF - Release/update a disk buffer
 *
 * Releases a buffer obtained from DBUF_$GET_BLOCK and optionally marks
 * it for writeback.
 *
 * Parameters:
 *   buffer - Pointer to the buffer data (from DBUF_$GET_BLOCK)
 *   flags  - Buffer operation flags (can be OR'd together):
 *            0x01 = mark dirty (deferred write)
 *            0x02 = write back if dirty
 *            0x04 = invalidate buffer
 *            0x08 = decrement reference count (release)
 *
 *            Common combinations:
 *            0x08 = release without write
 *            0x09 = mark dirty and release
 *            0x0B = write back immediately and release
 *   status - Receives status code
 *
 * Original address: 0x00e3a8b6
 */
extern void DBUF_$SET_BUFF(void *buffer, ushort flags, status_$t *status);

#endif /* DBUF_H */
