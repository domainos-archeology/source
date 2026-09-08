/*
 * BAT_$ADD_PART_VTOCE - Add VTOCE to partition chain
 *
 * Updates the partition's VTOCE chain with a new or updated block.
 *
 * Original address: 0x00E3AE2E
 */

#include "bat/bat_internal.h"

/*
 * BAT_$ADD_PART_VTOCE
 *
 * Parameters:
 *   vol_idx - Volume index (0-6)
 *   block   - VTOCE block number
 *   status  - Output status code
 *
 * Returns:
 *   Previous VTOCE block in partition chain (24-bit value)
 *
 * The new block is stored WITHOUT being masked to 24 bits - see the note at
 * the store below.
 *
 * Assembly analysis:
 *   - Takes ML_LOCK_BAT for thread safety
 *   - Calculates partition index from block number
 *   - Swaps current partition VTOCE block with new block
 *   - Returns the old VTOCE block value
 */
uint32_t BAT_$ADD_PART_VTOCE(int16_t vol_idx, uint32_t block, status_$t *status)
{
    bat_$volume_t *vol;
    bat_$partition_t *part;
    int16_t partition_idx;
    uint32_t old_vtoce;

    ML_$LOCK(ML_LOCK_BAT);

    *status = status_$ok;
    vol = &bat_$volumes[vol_idx];

    /* Calculate partition index from block number */
    if (block < vol->partition_start_offset) {
        partition_idx = 0;
    } else {
        partition_idx = (int16_t)M$DIS$LLL(block - vol->partition_start_offset,
                                            vol->partition_size);
    }

    part = &vol->partitions[partition_idx];

    /*
     * 0x00E3AE86-0x00E3AEA4, reproduced exactly:
     *
     *   00e3ae86  move.l  #0xffffff,D2
     *   00e3ae94  and.l   (-0x204,A0),D2        ; result = OLD chain & 0xFFFFFF
     *   00e3ae98  andi.l  #-0x1000000,(-0x204,A0)
     *   00e3aea0  move.l  (0xa,A6),D1           ; the new block, UNMASKED
     *   00e3aea4  or.l    D1,(-0x204,A0)
     *
     * Only the value returned to the caller is masked to 24 bits.  The
     * store ORs the caller's block over the whole longword, so a block
     * with anything set above bit 23 corrupts the partition status byte -
     * the same unmasked store BAT_$ALLOC_VTOCE makes at 0x00E3B08E.
     * BAT_SET_PART_CHAIN_LONG writes the status byte too, which is what
     * makes that reachable; the three-byte BAT_SET_VTOCE_BLOCK used here
     * before did not.
     */
    old_vtoce = BAT_PART_CHAIN_LONG(part) & 0x00FFFFFFu;

    BAT_SET_PART_CHAIN_LONG(part,
        (BAT_PART_CHAIN_LONG(part) & 0xFF000000u) | block);

    ML_$UNLOCK(ML_LOCK_BAT);

    return old_vtoce;
}
