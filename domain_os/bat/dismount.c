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
 * Argument block (link.w A6,-0x14 at 0x00E3B8BE):
 *
 *   (0x08,A6)  word  vol_idx     - 0x00E3B8CC
 *   (0x0a,A6)  BYTE  flags       - 0x00E3B8D0 move.b (0xa,A6),D3b
 *   (0x0c,A6)  long  status      - 0x00E3B8D6
 *
 * TODO: argument 2 is a Domain byte boolean, not a word -- 0x00E3B8D0 reads
 * it with `move.b` at the even offset and the caller at 0x00E386DE pushes it
 * with `st -(SP)`.  It stays `int16_t` here because both callers live in
 * vtoc/ and encode the byte in the high half of a word; retyping it means
 * changing them in the same pass.  Tracked by bead source-xlyv.
 *
 * Parameters:
 *   vol_idx - Volume index (1-6)
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

    /*
     * Copy the BAT header back into the label: BAT_HEADER_LONGWORDS
     * longwords from bat_$volume_t +0x00 into label +0x2C.
     *
     *   0x00E3B958  lea (-0x234,A2),A1      ; source = volume record + 0x00
     *   0x00E3B95C  lea (0x2c,A0),A4        ; dest   = label + 0x2C
     *   0x00E3B960  moveq #0x7,D0
     *   0x00E3B964  move.l (A1)+,(A4)+
     *   0x00E3B966  dbf D0w,0x00e3b964
     */
    {
        const uint32_t *src = (const uint32_t *)&vol->total_blocks;
        uint32_t *dst = (uint32_t *)&label->total_blocks;

        for (i = 0; i < BAT_HEADER_LONGWORDS; i++) {
            *dst++ = *src++;
        }
    }

    /*
     * For a new-format volume, copy the partition table back:
     * BAT_PART_TABLE_LONGWORDS longwords from bat_$volume_t +0x20 into
     * label +0xFC, i.e. 0x20C bytes -- the cells at +0x22C and +0x230 are
     * derived from the drive's geometry and are never written to disk.
     *
     *   0x00E3B96C  tst.b (0xd3f,A1)        ; new-format flag
     *   0x00E3B970  bpl.b 0x00e3b986        ; old format: skip the copy
     *   0x00E3B972  lea (-0x214,A2),A4      ; source = volume record + 0x20
     *   0x00E3B976  lea (0xfc,A0),A1        ; dest   = label + 0xFC
     *   0x00E3B97A  move.w #0x82,D0w
     *   0x00E3B980  move.l (A4)+,(A1)+
     *   0x00E3B982  dbf D0w,0x00e3b980
     */
    if (bat_$volume_flags[vol_idx] < 0) {
        const uint32_t *src = (const uint32_t *)&vol->num_partitions;
        uint32_t *dst = (uint32_t *)&label->num_partitions;

        for (i = 0; i < BAT_PART_TABLE_LONGWORDS; i++) {
            *dst++ = *src++;
        }
    }

    /*
     * 0x00E3B986..0x00E3B990: ONE clock reading is stored into TWO cells,
     * the label's +0xB0 and its +0xC0 -- not into dismount_time at +0xBC,
     * which BAT_$MOUNT stamps (0x00E3B7D0) and BAT_$DISMOUNT leaves alone.
     *
     *   0x00E3B986  move.l (0x00e2b0e4).l,D0
     *   0x00E3B98C  move.l D0,(0xb0,A0)
     *   0x00E3B990  move.l D0,(0xc0,A0)
     */
    current_time = TIME_$CURRENT_CLOCKH;
    label->mount_time_high = current_time;
    label->current_time = current_time;

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
