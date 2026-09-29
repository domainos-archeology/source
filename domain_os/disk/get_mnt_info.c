/*
 * DISK_$GET_MNT_INFO - Report a mounted or assigned volume's geometry
 *
 * 0x00E6BE4A - 0x00E6C05E (534 bytes).  Re-emitted from the disassembly on
 * 2026-09-19.  Wrong before: the partition-word packing loop was left
 * unemitted, vol_start was read from the re-resolved descriptor (the image
 * reads it from the ORIGINAL one at 0x00E6BED0, before the LV -> PV
 * switch takes effect), and the final `andi.w #0xfe00` was a big-endian
 * word store.
 *
 * Arguments:
 *   (0x8,A6)  vol_idx_ptr -> word volume index (D2)
 *   (0xc,A6)  param_2     never read (the caller's record size)
 *   (0x10,A6) info        -> disk_$mnt_info_t (D3)
 *   (0x14,A6) status      -> status_$t (A2)
 *
 * For a logical volume (lv_start != 0) bit 6 of the flags byte is set and
 * everything from vol_end on is taken from the backing physical volume,
 * part_volx[1] (0x00E6BEC2 `move.w (-0x10,A0),D2w`).
 *
 * Each partition word (info +0x16 + 2k, k = 0..num_parts-1) is built by
 * byte operations on a word the loop above just cleared:
 *   high byte: bits 3..7 = device_type low byte << 3 (resolved volume),
 *              then the low 3 bits replaced by an OR of the partition
 *              volume's controller low byte (0x00E6BFA0 ORs the whole byte)
 *   low byte:  high nibble = partition volume's dev_unit low byte << 4
 * The flag byte at +0x28 is bit 7 = mounted (state 3), bit 6 = LV, bit 5 =
 * driver flags bit 15 clear, bit 4 = as_options bit 0 (write protect),
 * bit 3 = driver flags bit 11, bit 2 = driver flags bit 13, bit 1 = driver
 * flags bit 9; the closing `andi.w #-0x200,(0x28,A2)` clears bit 0 of
 * that byte and all of byte +0x29.
 */

#include "disk/disk_internal.h"
#include "ml/ml.h"

/* The driver record words this routine reads (0x00E6BEF8, 0x00E6BF78,
 * 0x00E6BF9C, 0x00E6BFDA). */
#define DISK_MNT_DEV_TYPE_OFFSET   0x04
#define DISK_MNT_DEV_CTRL_OFFSET   0x06
#define DISK_MNT_DEV_FLAGS_OFFSET  0x08

static inline uint16_t disk_$mnt_dev_word(const void *dev, int off)
{
    return *(const uint16_t *)((const uint8_t *)dev + off);
}

void DISK_$GET_MNT_INFO(uint16_t *vol_idx_ptr, void *param_2, void *info_p,
                        status_$t *status)
{
    disk_$mnt_info_t *info = (disk_$mnt_info_t *)info_p;
    uint16_t vol_idx;           /* D2 */
    disk_$volume_t *orig;       /* A0 */
    disk_$volume_t *vol;        /* A3 / D2 */
    disk_$volume_t *pvol;       /* A3 in the partition loop */
    const void *dev;            /* A4 */
    uint16_t dev_flags;         /* D0 at 0x00E6C004 */
    uint16_t w, hi, lo;
    int16_t parts;
    int16_t k;

    (void)param_2;

    /* 0x00E6BE5A - 0x00E6BE74: `btst.l D0,D1` against 0x7fe, modulo 32;
     * an invalid index returns without taking the lock */
    vol_idx = *vol_idx_ptr;
    if ((((uint32_t)VALID_VOL_MASK >> (vol_idx & 0x1f)) & 1u) == 0) {
        *status = status_$invalid_volume_index;
        return;
    }

    /* 0x00E6BE78 - 0x00E6BE84 */
    ML_$EXCLUSION_START(&PMAP_$DATA.mount_lock);

    /* 0x00E6BE86 - 0x00E6BEB0 */
    orig = DISK_VOL(vol_idx);
    if (orig->mount_state != DISK_MOUNT_MOUNTED &&
        orig->mount_state != DISK_MOUNT_ASSIGNED) {
        *status = status_$volume_not_properly_mounted;
        ML_$EXCLUSION_STOP(&PMAP_$DATA.mount_lock);                        /* 0x00E6C04A */
        return;
    }

    /* 0x00E6BEB4 - 0x00E6BECA */
    if (orig->lv_start != 0) {
        info->flags |= DISK_MNT_FLAG_LOGICAL_VOLUME;            /* bset.b #6 */
        vol_idx = orig->part_volx[1];
    } else {
        info->flags &= (uint8_t)~DISK_MNT_FLAG_LOGICAL_VOLUME;  /* bclr.b #6 */
    }

    /* 0x00E6BED0: from the ORIGINAL descriptor */
    info->vol_start = orig->addr_start;

    /* 0x00E6BED4 - 0x00E6BF0A: from the resolved one */
    vol = DISK_VOL(vol_idx);
    *status = status_$ok;
    info->vol_end = vol->addr_end;
    dev = vol->dev_info;
    info->dev_type = disk_$mnt_dev_word(dev, DISK_MNT_DEV_TYPE_OFFSET);
    info->unit_id = vol->unit_id;
    info->bat_step = vol->bat_step;
    /* one `move.l (-0x28,A3),(0xe,A1)`: sec_per_track then num_heads */
    info->sectors_per_track = vol->sec_per_track;
    info->heads = vol->num_heads;

    /* 0x00E6BF10 - 0x00E6BF34: any other code leaves +0x12 untouched */
    switch (vol->sector_size_code) {
    case 0:  info->sectors_per_block = 1; break;
    case 1:  info->sectors_per_block = 2; break;
    case 2:  info->sectors_per_block = 4; break;
    default: break;
    }

    /* 0x00E6BF3A - 0x00E6BF40 */
    info->n_partitions = vol->num_parts;
    info->interleave = vol->part_volx[0];

    /* 0x00E6BF46 - 0x00E6BF52: moveq #7 / dbf = eight words cleared */
    for (k = 0; k < 8; k++) {
        info->part_info[k] = 0;
    }

    /* 0x00E6BF56 - 0x00E6BFB8: one word per partition, k = 1..num_parts */
    parts = (int16_t)vol->num_parts;
    if ((int16_t)(parts - 1) >= 0) {
        for (k = 1; k <= parts; k++) {
            w = info->part_info[k - 1];
            hi = (uint16_t)((w >> 8) & 0xff);
            lo = (uint16_t)(w & 0xff);
            /* 0x00E6BF6C - 0x00E6BF7E: device type low byte << 3 */
            hi = (uint16_t)((hi & 0x07) |
                 ((disk_$mnt_dev_word(dev, DISK_MNT_DEV_TYPE_OFFSET) << 3) & 0xff));
            /* 0x00E6BF82 - 0x00E6BFA0: partition volume's controller low
             * byte, ORed in whole after clearing the low three bits */
            pvol = DISK_VOL(vol->part_volx[k]);
            hi = (uint16_t)((hi & 0xf8) |
                 (disk_$mnt_dev_word(pvol->dev_info, DISK_MNT_DEV_CTRL_OFFSET) & 0xff));
            /* 0x00E6BFA4 - 0x00E6BFB0: dev_unit low byte << 4 (-0x2b) */
            lo = (uint16_t)((lo & 0x0f) | ((pvol->dev_unit << 4) & 0xff));
            info->part_info[k - 1] = (uint16_t)((hi << 8) | lo);
        }
    }

    /* 0x00E6BFBC - 0x00E6BFD2: bit 7 = mounted */
    info->flags = (uint8_t)((info->flags & 0x7f) |
                            ((vol->mount_state == DISK_MOUNT_MOUNTED) ? 0x80 : 0));

    /* 0x00E6BFD6 - 0x00E6BFEA: bit 5 = driver flags word not negative */
    dev_flags = disk_$mnt_dev_word(dev, DISK_MNT_DEV_FLAGS_OFFSET);
    info->flags = (uint8_t)((info->flags & 0xdf) |
                            (((int16_t)dev_flags >= 0) ? 0x20 : 0));

    /* 0x00E6BFEE - 0x00E6C000: bit 4 = as_options bit 0 (byte -0x1f) */
    info->flags = (uint8_t)((info->flags & 0xef) |
                            ((vol->as_options & DISK_VOL_FLAG_WRITE_PROTECT) ? 0x10 : 0));

    /* 0x00E6C004 - 0x00E6C040: bits 2, 3, 1 from driver flag bits 13, 11, 9 */
    info->flags = (uint8_t)((info->flags & 0xfb) | ((dev_flags & 0x2000) ? 0x04 : 0));
    info->flags = (uint8_t)((info->flags & 0xf7) | ((dev_flags & 0x0800) ? 0x08 : 0));
    info->flags = (uint8_t)((info->flags & 0xfd) | ((dev_flags & 0x0200) ? 0x02 : 0));

    /* 0x00E6C044: `andi.w #-0x200,(0x28,A2)` - bit 0 of +0x28 and all of
     * +0x29 cleared */
    info->flags &= 0xfe;
    info->_pad_29 = 0;

    /* 0x00E6C04A - 0x00E6C050 */
    ML_$EXCLUSION_STOP(&PMAP_$DATA.mount_lock);
}
