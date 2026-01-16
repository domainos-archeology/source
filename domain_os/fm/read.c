/*
 * FM_$READ - Read a file map entry
 *
 * Reads a 128-byte file map entry from either a VTOCE or a file map block.
 * The entry contains 32 block pointers used for indirect block addressing.
 *
 * Original address: 0x00e3a314
 */

#include "fm/fm_internal.h"

/*
 * FM_$READ
 *
 * Parameters:
 *   file_ref    - File reference (vol_idx at +0x1c, uid at +0x08)
 *   block_addr  - Block address (high 28 bits = block, low 4 bits = entry index)
 *   level       - 0 = read from VTOCE, non-zero = read from file map block
 *   entry_out   - Output buffer for 128-byte file map entry
 *   status      - Output status code
 */
void FM_$READ(fm_$file_ref_t *file_ref, uint32_t block_addr, uint16_t level,
              fm_$entry_t *entry_out, status_$t *status)
{
    uint8_t vol_idx;
    uint32_t block_num;
    uint8_t entry_idx;
    uid_t *uid_ptr;
    uid_t local_uid;
    uint32_t param4;
    uint16_t flags;
    void *buffer;
    uint32_t *src_ptr;
    uint32_t *dst_ptr;
    int i;

    /* Extract volume index from file reference */
    vol_idx = file_ref->vol_idx;

    /* Extract block number and entry index from block address */
    block_num = block_addr >> 4;
    entry_idx = (uint8_t)(block_addr & 0x0F);

    /* Acquire disk lock */
    ML_$LOCK(FM_LOCK_ID);

    /* Check if volume is mounted */
    if (!VTOC_IS_MOUNTED(vol_idx)) {
        *status = status_$VTOC_not_mounted;
        goto done;
    }

    /*
     * Special case: If volume is write-protected (cached lookups flag set)
     * and requesting block 1, entry 15 (unused allocation bitmap area),
     * return a zeroed entry.
     */
    if (VTOC_CACH_LOOKUPS[vol_idx] < 0 && block_num == 1 && entry_idx == 0x0F) {
        *status = status_$ok;
        /* Zero out the output entry */
        dst_ptr = entry_out->blocks;
        for (i = 0; i < 32; i++) {
            *dst_ptr++ = 0;
        }
        goto done;
    }

    /*
     * Set up parameters for DBUF_$GET_BLOCK based on level
     */
    if (level == 0) {
        /* Level 0: Reading from VTOCE - use VTOC_$UID */
        flags = 0;
        uid_ptr = &VTOC_$UID;
        param4 = block_num;
    } else {
        /*
         * Level != 0: Reading from file map block - use file's UID
         *
         * Calculate param4 based on level:
         *   param4 = ((level - 1) / 8) * 256 + 32
         * This encodes the block range threshold for the indirection level.
         */
        int level_adj = level - 1;
        if (level_adj < 0) {
            level_adj = level + 6;  /* Handle edge case (shouldn't happen) */
        }
        param4 = (level_adj >> 3) * 0x100 + 0x20;
        flags = 1;
        uid_ptr = &file_ref->file_uid;
    }

    /* Copy UID to local variable */
    local_uid.high = uid_ptr->high;
    local_uid.low = uid_ptr->low;

    /* Get the disk block into a buffer */
    buffer = DBUF_$GET_BLOCK(vol_idx, block_num, &local_uid, param4,
                             (uint32_t)flags << 16, status);
    if (*status != status_$ok) {
        goto done;
    }

    /*
     * Calculate pointer to entry within the buffer based on level and format
     */
    if (level != 0) {
        /* File map block: entries are 0x80 bytes each */
        src_ptr = (uint32_t *)((uint8_t *)buffer + (uint32_t)entry_idx * FM_ENTRY_SIZE);
    } else {
        /* VTOCE block: offset depends on volume format */
        if (VTOC_IS_NEW_FORMAT(vol_idx)) {
            /* New format: entry at offset 0xD8 within 0x150-byte VTOCE */
            src_ptr = (uint32_t *)((uint8_t *)buffer +
                                   (uint32_t)entry_idx * FM_VTOCE_NEW_SIZE +
                                   FM_VTOCE_NEW_OFFSET);
        } else {
            /* Old format: entry at offset 0x44 within 0xCC-byte VTOCE */
            src_ptr = (uint32_t *)((uint8_t *)buffer +
                                   (uint32_t)entry_idx * FM_VTOCE_OLD_SIZE +
                                   FM_VTOCE_OLD_OFFSET);
        }
    }

    /* Copy 32 longwords (128 bytes) to output buffer */
    dst_ptr = entry_out->blocks;
    for (i = 0; i < 32; i++) {
        *dst_ptr++ = *src_ptr++;
    }

    /* Release the buffer (no write needed) */
    DBUF_$SET_BUFF(buffer, FM_BUF_RELEASE, status);

done:
    /* Release disk lock */
    ML_$UNLOCK(FM_LOCK_ID);
}
