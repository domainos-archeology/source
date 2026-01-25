/*
 * BAT - Block Allocation Table
 *
 * This module manages disk block allocation for Domain/OS volumes.
 * The BAT uses a bitmap to track free/allocated blocks, with partitions
 * to organize the disk space and VTOCE (Volume Table of Contents Entry)
 * allocation for file metadata.
 *
 * Each volume can have up to 0x83 (131) partitions, with each partition
 * tracking its own free block count and VTOCE chain.
 */

#ifndef BAT_H
#define BAT_H

#include "base/base.h"

/*
 * BAT UID - Block Allocation Table unique identifier
 * Address: 0xE173A4
 */
extern uid_t BAT_$UID;

/*
 * BAT Lock ID - Used with ML_$LOCK/ML_$UNLOCK
 */
#define ML_LOCK_BAT     0x11

/*
 * Maximum number of volumes supported
 */
#define BAT_MAX_VOLUMES     7

/*
 * Status codes
 */
#define bat_$not_mounted            0x10004
#define bat_$invalid_block          0x80010003
#define bat_$error                  0x80010001

/*
 * BAT_$ALLOCATE - Allocate disk blocks
 *
 * Allocates blocks from the volume's free block pool. Searches the BAT
 * bitmap starting near the hint block and allocates up to count blocks.
 *
 * @param vol_idx    Volume index (0-6)
 * @param hint       Hint block number for locality
 * @param count      Number of blocks to allocate (low 16 bits) and flags (high 16 bits)
 *                   High word: 0 = normal allocation, 1 = reserved allocation
 * @param blocks_out Output array receiving allocated block numbers
 * @param status     Output status code
 */
void BAT_$ALLOCATE(int16_t vol_idx, uint32_t hint, uint32_t count,
                   uint32_t *blocks_out, status_$t *status);

/*
 * BAT_$FREE - Free disk blocks
 *
 * Returns blocks to the volume's free block pool.
 *
 * @param blocks     Array of block addresses to free
 * @param count      Number of blocks to free
 * @param vol_idx    Volume index (0-6)
 * @param reserved   If non-zero, return blocks to reserved pool
 * @param status     Output status code
 */
void BAT_$FREE(uint32_t *blocks, int16_t count, int16_t vol_idx,
               int16_t reserved, status_$t *status);

/*
 * BAT_$ALLOC_FM - Allocate a block using First Match algorithm
 *
 * Allocates a single block by searching partitions for available space.
 * Uses a first-match strategy starting from the middle partition.
 *
 * @param vol_idx    Volume index (0-6)
 * @param status     Output status code
 *
 * @return           Allocated block number, or 0 on failure
 */
uint32_t BAT_$ALLOC_FM(int16_t vol_idx, status_$t *status);

/*
 * BAT_$ALLOC_VTOCE - Allocate a VTOCE block
 *
 * Allocates a Volume Table of Contents Entry block for file metadata.
 * May create a new VTOCE block if existing ones are full.
 *
 * @param vol_idx      Volume index (0-6)
 * @param hint         Hint block for locality (or 0 for any)
 * @param block_out    Output receiving allocated VTOCE block number
 * @param status       Output status code
 * @param new_vtoce    Output: non-zero if a new VTOCE block was allocated
 */
void BAT_$ALLOC_VTOCE(int16_t vol_idx, uint32_t hint, uint32_t *block_out,
                      status_$t *status, int8_t *new_vtoce);

/*
 * BAT_$ADD_PART_VTOCE - Add VTOCE to partition chain
 *
 * Updates the partition's VTOCE chain with a new or updated block.
 *
 * @param vol_idx    Volume index (0-6)
 * @param block      VTOCE block number
 * @param status     Output status code
 *
 * @return           Previous VTOCE block in chain
 */
uint32_t BAT_$ADD_PART_VTOCE(int16_t vol_idx, uint32_t block, status_$t *status);

/*
 * BAT_$MOUNT - Mount a volume's BAT
 *
 * Initializes the BAT data structures for a volume by reading the
 * volume label and partition information from disk.
 *
 * @param vol_idx    Volume index (0-6)
 * @param salvage_ok If negative, skip salvage check
 * @param status     Output status code
 */
void BAT_$MOUNT(int16_t vol_idx, int8_t salvage_ok, status_$t *status);

/*
 * BAT_$DISMOUNT - Dismount a volume's BAT
 *
 * Flushes and releases BAT data structures for a volume.
 * Updates the volume label with current statistics if requested.
 *
 * @param vol_idx    Volume index (0-6)
 * @param flags      If negative, don't write label; otherwise write updated stats
 * @param status     Output status code
 */
void BAT_$DISMOUNT(int16_t vol_idx, int16_t flags, status_$t *status);

/*
 * BAT_$N_FREE - Get free block count
 *
 * Returns the number of free and total blocks on a volume.
 *
 * @param vol_idx_ptr  Pointer to volume index
 * @param free_out     Output receiving free block count
 * @param total_out    Output receiving total block count
 * @param status       Output status code
 */
void BAT_$N_FREE(uint16_t *vol_idx_ptr, uint32_t *free_out, uint32_t *total_out,
                 status_$t *status);

/*
 * BAT_$RESERVE - Reserve blocks for future allocation
 *
 * Moves blocks from the free pool to the reserved pool.
 * Reserved blocks can only be allocated with the reserved flag.
 *
 * @param vol_idx    Volume index (0-6)
 * @param count      Number of blocks to reserve
 * @param status     Output status code
 */
void BAT_$RESERVE(int16_t vol_idx, uint32_t count, status_$t *status);

/*
 * BAT_$CANCEL - Cancel reserved blocks
 *
 * Moves blocks from the reserved pool back to the free pool.
 *
 * @param vol_idx    Volume index (0-6)
 * @param count      Number of blocks to cancel
 * @param status     Output status code
 */
void BAT_$CANCEL(int16_t vol_idx, uint32_t count, status_$t *status);

/*
 * BAT_$GET_BAT_STEP - Get BAT allocation step
 *
 * Returns the BAT step value for a volume, which controls
 * block allocation locality.
 *
 * @param vol_idx    Volume index (0-6)
 *
 * @return           BAT step value
 */
uint16_t BAT_$GET_BAT_STEP(int16_t vol_idx);

#endif /* BAT_H */
