/*
 * DISK_$GET_MNT_INFO - Get mount information for a volume
 *
 * Returns detailed mount information for an assigned or mounted volume,
 * including disk UIDs, partition info, and device flags.
 *
 * The info structure (44 bytes) contains (0xe6bed0-0xe6bf52):
 *   +0x00: Volume address range start (4 bytes, descriptor +0x88)
 *   +0x04: Volume address range end (4 bytes, descriptor +0x8c)
 *   +0x08: Device type (2 bytes, dev_info +4)
 *   +0x0a: Unit id (2 bytes, descriptor +0x9a)
 *   +0x0c: descriptor +0xa2, lv_shift (2 bytes)
 *   +0x0e: Sectors per track and head count (4 bytes, descriptor +0x9c)
 *   +0x12: 1 << sector_size_code, i.e. the hardware sectors per
 *          disk block (2 bytes: 1, 2 or 4)
 *   +0x14: Number of partitions (2 bytes)
 *   +0x16-0x25: Partition info array (16 bytes)
 *   +0x26: descriptor +0xb2, the interleave mode (2 bytes)
 *   +0x28: Flags byte
 *
 * @param vol_idx_ptr  Pointer to volume index
 * @param param_2      Unknown parameter
 * @param info         Output: Mount info structure (44 bytes)
 * @param status       Output: Status code
 */

#include "disk/disk_internal.h"

/* disk_$volume_t, DISK_VOL(), VALID_VOL_MASK, DISK_MOUNT_ASSIGNED and
 * DISK_VOL_FLAG_WRITE_PROTECT come from disk/disk_internal.h.  The "unit"
 * this function reports is unit_id (+0x9a), not the dev_unit (+0x98) that
 * DISK_$DISMOUNT and DISK_$LV_ASSIGN match on. */

void DISK_$GET_MNT_INFO(uint16_t *vol_idx_ptr, void *param_2, void *info,
                         status_$t *status)
{
    uint16_t vol_idx;
    disk_$volume_t *vol;
    uint8_t *info_bytes = (uint8_t *)info;
    uint16_t mount_state;
    void *dev_info;
    uint16_t dev_flags;
    int16_t sector_size_type;
    int16_t num_parts;
    int16_t i;

    vol_idx = *vol_idx_ptr;

    /* Validate volume index (must be 1-10) */
    if ((((uint32_t)1 << (vol_idx & 0x1f)) & VALID_VOL_MASK) == 0) {
        *status = status_$invalid_volume_index;
        return;
    }

    ML_$EXCLUSION_START(&MOUNT_LOCK);

    vol = DISK_VOL(vol_idx);

    mount_state = vol->mount_state;

    if (mount_state != DISK_MOUNT_MOUNTED && mount_state != DISK_MOUNT_ASSIGNED) {
        *status = status_$volume_not_properly_mounted;
        ML_$EXCLUSION_STOP(&MOUNT_LOCK);
        return;
    }

    /* Check for LV data and update vol_idx if needed (0xe6beb4) */
    if (vol->lv_start == 0) {
        info_bytes[0x28] &= 0xbf;  /* Clear bit 6 */
    } else {
        info_bytes[0x28] |= 0x40;  /* Set bit 6 */
        /* 0xe6bec2: entry 1 of the partition table is the backing PV */
        vol_idx = vol->part_volx[1];
    }

    /* Re-fetch the descriptor with the potentially new vol_idx */
    vol = DISK_VOL(vol_idx);

    *status = status_$ok;

    /* Address range (0xe6bed0 / 0xe6beee) */
    *(uint32_t *)info_bytes = vol->addr_start;
    *(uint32_t *)(info_bytes + 4) = vol->addr_end;

    /* Get device info and copy type */
    dev_info = vol->dev_info;
    *(uint16_t *)(info_bytes + 8) = *(uint16_t *)((uint8_t *)dev_info + 4);
    *(uint16_t *)(info_bytes + 0x0a) = vol->unit_id;
    *(uint16_t *)(info_bytes + 0x0c) = vol->lv_shift;
    /* 0xe6bf0a copies sec_per_track and num_heads together as one longword */
    *(uint32_t *)(info_bytes + 0x0e) =
        ((uint32_t)vol->sec_per_track << 16) | vol->num_heads;

    /* Encode sector size */
    sector_size_type = (int16_t)vol->sector_size_code;
    if (sector_size_type == 0) {
        *(uint16_t *)(info_bytes + 0x12) = 1;  /* 0xe6bf24 */
    } else if (sector_size_type == 1) {
        *(uint16_t *)(info_bytes + 0x12) = 2;  /* 0xe6bf2c */
    } else if (sector_size_type == 2) {
        *(uint16_t *)(info_bytes + 0x12) = 4;  /* 0xe6bf34 */
    }

    /* Copy partition count and misc info (0xe6bf3a / 0xe6bf40) */
    *(uint16_t *)(info_bytes + 0x14) = vol->num_parts;
    *(uint16_t *)(info_bytes + 0x26) = vol->part_volx[0];

    /* Clear partition info array */
    for (i = 0; i < 8; i++) {
        *(uint16_t *)(info_bytes + 0x16 + i * 2) = 0;
    }

    /* Fill partition info */
    num_parts = (int16_t)vol->num_parts - 1;
    if (num_parts >= 0) {
        for (i = 0; i <= num_parts; i++) {
            /* This fills partition details - complex bit manipulation */
            /* TODO(source-pxn): Full implementation requires more reverse engineering */
        }
    }

    /* Set flags in byte at +0x28 */
    /* Bit 7: mounted flag (mount_state == 3) */
    mount_state = vol->mount_state;
    info_bytes[0x28] &= 0x7f;
    if (mount_state == DISK_MOUNT_MOUNTED) {
        info_bytes[0x28] |= 0x80;
    }

    /* Get device flags and set remaining bits */
    dev_info = vol->dev_info;
    dev_flags = *(uint16_t *)((uint8_t *)dev_info + 8);

    /* Bit 5: not negative flag */
    info_bytes[0x28] &= 0xdf;
    if ((int16_t)dev_flags >= 0) {
        info_bytes[0x28] |= 0x20;
    }

    /* Bit 4: write protect flag */
    info_bytes[0x28] &= 0xef;
    if ((vol->as_options & DISK_VOL_FLAG_WRITE_PROTECT) != 0) {
        info_bytes[0x28] |= 0x10;
    }

    /* Bit 2: SCSI flag (0x2000) */
    info_bytes[0x28] &= 0xfb;
    if ((dev_flags & 0x2000) != 0) {
        info_bytes[0x28] |= 0x04;
    }

    /* Bit 3: some flag (0x800) */
    info_bytes[0x28] &= 0xf7;
    if ((dev_flags & 0x0800) != 0) {
        info_bytes[0x28] |= 0x08;
    }

    /* Bit 1: no track format (0x200) */
    info_bytes[0x28] &= 0xfd;
    if ((dev_flags & 0x0200) != 0) {
        info_bytes[0x28] |= 0x02;
    }

    /* Clear lower 9 bits of word at +0x28 */
    *(uint16_t *)(info_bytes + 0x28) &= 0xfe00;

    ML_$EXCLUSION_STOP(&MOUNT_LOCK);
}
