/*
 * BAT_$DISMOUNT - Dismount a volume's BAT
 *
 * Flushes and releases BAT data structures for a volume.
 * Updates the volume label with current statistics if requested.
 *
 * Original address: 0x00E3B8BE
 */

#include "bat/bat_internal.h"

/*
 * BAT_$DISMOUNT
 *
 * Parameters:
 *   vol_idx - Volume index (0-6)
 *   flags   - If negative, don't write label; otherwise write updated stats
 *   status  - Output status code
 *
 * Assembly analysis:
 *   - Takes ML_LOCK_BAT for thread safety
 *   - If cached buffer belongs to this volume, flush and clear it
 *   - Validates volume is mounted
 *   - Clears mount status
 *   - If flags >= 0, updates volume label with current statistics
 *   - Copies partition info back to label for new format volumes
 *   - Updates timestamps and clears salvage flag
 *   - Ignores write-protected and storage-stopped errors
 */
void BAT_$DISMOUNT(int16_t vol_idx, int16_t flags, status_$t *status)
{
    bat_$label_t *label;
    bat_$volume_t *vol;
    uint32_t current_time;
    int16_t i;
    status_$t local_status;

    ML_$LOCK(ML_LOCK_BAT);

    /* If cached buffer belongs to this volume, flush it */
    if (vol_idx == bat_$cached_vol) {
        if (bat_$cached_buffer != NULL) {
            DBUF_$SET_BUFF(bat_$cached_buffer, bat_$cached_dirty, &local_status);
        }
        bat_$cached_buffer = NULL;
        bat_$cached_vol = 0;
    }

    /* Check if volume is mounted */
    if (bat_$mounted[vol_idx] >= 0) {
        *status = bat_$not_mounted;
        goto done;
    }

    /* Clear mount status */
    bat_$mounted[vol_idx] = 0;

    vol = &bat_$volumes[vol_idx];

    /* If flags is negative, just clear volume without updating label */
    if (flags < 0) {
        vol->total_blocks = 0;
        *status = status_$ok;
        goto done;
    }

    /* Read volume label to update it */
    label = (bat_$label_t *)DBUF_$GET_BLOCK(vol_idx, 0, (void *)&LV_LABEL_$UID,
                                             0, 0, status);
    if (*status != status_$ok) {
        goto done;
    }

    /* Copy volume statistics back to label (8 longwords at offset 0x2C) */
    label->total_blocks = vol->total_blocks;
    label->free_blocks = vol->free_blocks;
    label->bat_block_start = vol->bat_block_start;
    label->first_data_block = vol->first_data_block;
    label->step_blocks = vol->step_blocks;
    label->bat_step = vol->bat_step;
    label->reserved_blocks = vol->reserved_blocks;

    /* For new format volumes, copy partition table back */
    if ((bat_$volume_flags[vol_idx] >> 24) & 0x80) {
        uint32_t *src = (uint32_t *)&vol->num_partitions;
        uint32_t *dst = (uint32_t *)&label->num_partitions;
        for (i = 0; i <= 0x82; i++) {
            *dst++ = *src++;
            *dst++ = *src++;
        }
    }

    /* Update timestamps */
    current_time = TIME_$CURRENT_CLOCKH;
    label->mount_time_high = current_time;
    label->dismount_time = current_time;

    /* Clear salvage flag - volume is clean */
    label->salvage_flag = 0;

    /* Write back label */
    DBUF_$SET_BUFF(label, BAT_BUF_WRITEBACK, status);

    /* Ignore write-protected and storage-stopped errors */
    if (*status == status_$disk_write_protected ||
        *status == status_$storage_module_stopped) {
        *status = status_$ok;
    }

done:
    ML_$UNLOCK(ML_LOCK_BAT);
}
