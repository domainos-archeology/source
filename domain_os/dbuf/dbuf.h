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
 * Parameters:
 *   vol_idx  - Volume index
 *   block    - Block number to read
 *   uid      - Pointer to UID for validation
 *   param4   - Additional parameter (purpose TBD)
 *   param5   - Additional parameter (purpose TBD)
 *   status   - Receives status code
 *
 * Returns:
 *   Pointer to the buffer containing the block data, or NULL on error
 *
 * Original address: TBD
 */
extern void *DBUF_$GET_BLOCK(ushort vol_idx, int block, void *uid,
                             int param4, int param5, status_$t *status);

/*
 * DBUF_$SET_BUFF - Release/update a disk buffer
 *
 * Releases a buffer obtained from DBUF_$GET_BLOCK and optionally marks
 * it for writeback.
 *
 * Parameters:
 *   buffer - Pointer to the buffer (from DBUF_$GET_BLOCK)
 *   flags  - Buffer flags:
 *            0x08 = release without write
 *            0x0B = write back and release
 *   status - Receives status code
 *
 * Original address: TBD
 */
extern void DBUF_$SET_BUFF(void *buffer, short flags, status_$t *status);

#endif /* DBUF_H */
