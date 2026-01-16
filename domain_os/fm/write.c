/*
 * FM_$WRITE - Write a file map entry
 *
 * Writes a 128-byte file map entry to either a VTOCE or a file map block.
 * The entry contains 32 block pointers used for indirect block addressing.
 *
 * Original address: 0x00e3a45c
 */

#include "fm/fm_internal.h"

/*
 * FM_$WRITE
 *
 * Parameters:
 *   file_ref    - File reference (vol_idx at +0x1c, uid at +0x08)
 *   block_addr  - Block address (high 28 bits = block, low 4 bits = entry index)
 *   level       - 0 = write to VTOCE, non-zero = write to file map block
 *   entry_in    - Input buffer containing 128-byte file map entry
 *   flags       - Write flags (bit 7 set = immediate writeback)
 *   status      - Output status code
 */
void FM_$WRITE(fm_$file_ref_t *file_ref, uint32_t block_addr, uint16_t level,
               fm_$entry_t *entry_in, char flags, status_$t *status)
{
    uint8_t vol_idx;
    uint32_t block_num;
    uint8_t entry_idx;
    uid_t *uid_ptr;
    uid_t local_uid;
    uint32_t param4;
    uint16_t get_flags;
    uint16_t buf_flags;
    void *buffer;
    uint32_t *src_ptr;
    uint32_t *dst_ptr;
    int i;

    /* Extract volume index from file reference */
    vol_idx = file_ref->vol_idx;

    /* Determine buffer flags based on write flags */
    if (flags < 0) {
        /* Bit 7 set: immediate writeback */
        buf_flags = FM_BUF_WRITEBACK;
    } else {
        /* Bit 7 clear: dirty, write later */
        buf_flags = FM_BUF_DIRTY;
    }

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
     * If volume has cached lookups (write-protected/read-only optimization),
     * silently succeed without actually writing.
     */
    if (VTOC_CACH_LOOKUPS[vol_idx] < 0) {
        *status = status_$ok;
        goto done;
    }

    /*
     * Set up parameters for DBUF_$GET_BLOCK based on level
     */
    if (level == 0) {
        /* Level 0: Writing to VTOCE - use VTOC_$UID */
        get_flags = 0;
        uid_ptr = &VTOC_$UID;
        param4 = block_num;
    } else {
        /*
         * Level != 0: Writing to file map block - use file's UID
         *
         * Calculate param4 based on level:
         *   param4 = ((level - 1) / 8) * 256 + 32
         */
        int level_adj = level - 1;
        if (level_adj < 0) {
            level_adj = level + 6;
        }
        param4 = (level_adj >> 3) * 0x100 + 0x20;
        get_flags = 1;
        uid_ptr = &file_ref->file_uid;
    }

    /* Copy UID to local variable */
    local_uid.high = uid_ptr->high;
    local_uid.low = uid_ptr->low;

    /* Get the disk block into a buffer */
    buffer = DBUF_$GET_BLOCK(vol_idx, block_num, &local_uid, param4,
                             (uint32_t)get_flags << 16, status);
    if (*status != status_$ok) {
        goto done;
    }

    /*
     * Calculate pointer to entry within the buffer and copy data
     */
    src_ptr = entry_in->blocks;

    if (level != 0) {
        /* File map block: entries are 0x80 bytes each */
        dst_ptr = (uint32_t *)((uint8_t *)buffer + (uint32_t)entry_idx * FM_ENTRY_SIZE);
        /* Copy 32 longwords (128 bytes) from input buffer */
        for (i = 0; i < 32; i++) {
            *dst_ptr++ = *src_ptr++;
        }
    } else {
        /* VTOCE block: offset depends on volume format */
        if (VTOC_IS_NEW_FORMAT(vol_idx)) {
            /*
             * New format: entry at offset 0xD8 within 0x150-byte VTOCE
             * Note: The copy loop is different in the original for new format -
             * it copies src to dst (reverse of old format code path)
             */
            dst_ptr = (uint32_t *)((uint8_t *)buffer +
                                   (uint32_t)entry_idx * FM_VTOCE_NEW_SIZE +
                                   FM_VTOCE_NEW_OFFSET);
            for (i = 0; i < 32; i++) {
                *dst_ptr++ = *src_ptr++;
            }
        } else {
            /* Old format: entry at offset 0x44 within 0xCC-byte VTOCE */
            dst_ptr = (uint32_t *)((uint8_t *)buffer +
                                   (uint32_t)entry_idx * FM_VTOCE_OLD_SIZE +
                                   FM_VTOCE_OLD_OFFSET);
            for (i = 0; i < 32; i++) {
                *dst_ptr++ = *src_ptr++;
            }
        }
    }

    /* Release the buffer with appropriate dirty flags */
    DBUF_$SET_BUFF(buffer, buf_flags, status);

done:
    /* Release disk lock */
    ML_$UNLOCK(FM_LOCK_ID);
}
