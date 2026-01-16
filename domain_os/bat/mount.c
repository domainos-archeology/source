/*
 * BAT_$MOUNT - Mount a volume's BAT
 *
 * Initializes the BAT data structures for a volume by reading the
 * volume label and partition information from disk.
 *
 * Original address: 0x00E3B6F8
 */

#include "bat/bat_internal.h"

/*
 * BAT_$MOUNT
 *
 * Parameters:
 *   vol_idx    - Volume index (0-6)
 *   salvage_ok - If negative, skip salvage check; otherwise require clean volume
 *   status     - Output status code
 *
 * Assembly analysis:
 *   - Gets current time from TIME_$CURRENT_CLOCKH
 *   - Takes ML_LOCK_BAT for thread safety
 *   - Clears mount status, then reads volume label (block 0)
 *   - Determines volume format (old vs new) based on label.version
 *   - Checks salvage flag unless salvage_ok is negative
 *   - Copies volume statistics from label to bat_$volumes array
 *   - Copies partition table from label to bat_$volumes array
 *   - For old format volumes, sets up single partition covering all blocks
 *   - Calculates allocation chunk parameters from disk geometry
 *   - Updates label with mount time and marks buffer dirty
 */
void BAT_$MOUNT(int16_t vol_idx, int8_t salvage_ok, status_$t *status)
{
    uint32_t current_time;
    bat_$label_t *label;
    bat_$volume_t *vol;
    bat_$disk_info_t *dinfo;
    int16_t i;
    boolean needs_salvage;
    boolean is_new_format;
    uint32_t chunk_size;
    uint32_t chunk_offset;

    /* Capture current time before taking lock */
    current_time = TIME_$CURRENT_CLOCKH;

    ML_$LOCK(ML_LOCK_BAT);

    /* Clear mount status for this volume */
    bat_$mounted[vol_idx] = 0;

    /* Read volume label (block 0) */
    label = (bat_$label_t *)DBUF_$GET_BLOCK(vol_idx, 0, (void *)&LV_LABEL_$UID,
                                             0, 0, status);
    if (*status != status_$ok) {
        goto done;
    }

    vol = &bat_$volumes[vol_idx];

    /* Determine volume format based on version field */
    is_new_format = (label->version != 0);

    /* Store format flag in high byte of volume_flags */
    if (is_new_format) {
        bat_$volume_flags[vol_idx] |= 0xFF000000;
    } else {
        bat_$volume_flags[vol_idx] &= 0x00FFFFFF;
    }

    /* Check salvage flag */
    if (is_new_format) {
        needs_salvage = ((label->unknown_3c & 0x1000) != 0);
    } else {
        needs_salvage = (label->unknown_3c < 0);
    }

    /* Also check if salvage_flag field indicates salvage needed */
    if (label->salvage_flag == 1) {
        needs_salvage = true;
    }

    /* If salvage needed and not allowed, return error */
    if (needs_salvage && salvage_ok >= 0) {
        DBUF_$SET_BUFF(label, BAT_BUF_CLEAN, status);
        *status = status_$disk_needs_salvaging;
        goto done;
    }

    /* Mark volume as mounted */
    bat_$mounted[vol_idx] = (int8_t)0xFF;

    /* Update label timestamps */
    label->mount_time_high = current_time;

    /* Clear dirty bit in flags, set salvage flag based on needs_salvage */
    label->unknown_3c &= 0xFFEF;  /* Clear bit 4 */
    if (needs_salvage) {
        label->unknown_3c |= 0x10;  /* Set bit 4 */
    }

    /* Initialize step_blocks if zero */
    if (*(uint32_t *)&label->step_blocks == 0) {
        label->step_blocks = 0;
        label->bat_step = 3;
    }

    /* Update mount node info */
    label->mount_time_low &= 0xFFF00000;
    label->mount_time_low |= (NODE_$ME & 0x000FFFFF);

    /* Record boot time and current time */
    label->boot_time = TIME_$BOOT_TIME;
    label->dismount_time = current_time;

    /* Mark volume as needing salvage on disk until clean dismount */
    label->salvage_flag = 1;

    /* Copy volume statistics from label (8 longwords at offset 0x2C) */
    vol->total_blocks = label->total_blocks;
    vol->free_blocks = label->free_blocks;
    vol->bat_block_start = label->bat_block_start;
    vol->first_data_block = label->first_data_block;
    vol->unknown_10 = (uint16_t)(label->unknown_3c & 0xFFFF);
    vol->step_blocks = label->step_blocks;
    vol->bat_step = label->bat_step;
    vol->reserved_blocks = label->reserved_blocks;

    /* Copy partition table (0x83 entries starting at offset 0xFC) */
    /* Each entry is 8 bytes, total 0x418 bytes */
    {
        uint32_t *src = (uint32_t *)&label->num_partitions;
        uint32_t *dst = (uint32_t *)&vol->num_partitions;
        for (i = 0; i <= 0x82; i++) {
            *dst++ = *src++;
            *dst++ = *src++;
        }
    }

    /* Handle old format volumes - set up single partition */
    if (!is_new_format) {
        vol->partition_size = 0x7FFFFFFF;  /* Maximum size */
        vol->num_partitions = 1;
        vol->partitions[0].free_count = vol->free_blocks - 0xB;
    }

    /* Calculate allocation chunk parameters from disk geometry */
    dinfo = &bat_$disk_info[vol_idx];
    chunk_size = (uint32_t)dinfo->sectors_per_track;
    vol->alloc_chunk_size = chunk_size;

    if (dinfo->disk_type == 1) {
        /* Special disk type: multiply by track width */
        chunk_size = M$MIU$LLW(chunk_size,
                               (uint16_t)dinfo->sectors_per_track);
        vol->alloc_chunk_size = chunk_size;
    }

    /* Calculate chunk offset: (first_data_block + disk_offset) % chunk_size */
    chunk_offset = M$OIS$LLL(vol->first_data_block + dinfo->offset, chunk_size);
    vol->alloc_chunk_offset = chunk_size - chunk_offset;

    /* Write back label with updated info */
    DBUF_$SET_BUFF(label, BAT_BUF_WRITEBACK, status);

done:
    ML_$UNLOCK(ML_LOCK_BAT);
}
