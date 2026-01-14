/*
 * DISK_$GET_MNT_INFO - Get mount information for a volume
 *
 * Returns detailed mount information for an assigned or mounted volume,
 * including disk UIDs, partition info, and device flags.
 *
 * The info structure (44 bytes) contains:
 *   +0x00: Disk UID high (4 bytes)
 *   +0x04: Disk UID low (4 bytes)
 *   +0x08: Device type (2 bytes)
 *   +0x0a: Unit number (2 bytes)
 *   +0x0c: Something (2 bytes)
 *   +0x0e: Something (4 bytes)
 *   +0x12: Sector size encoding (2 bytes: 1=256, 2=512, 4=1024)
 *   +0x14: Number of partitions (2 bytes)
 *   +0x16-0x25: Partition info array (16 bytes)
 *   +0x26: Something (2 bytes)
 *   +0x28: Flags byte
 *
 * @param vol_idx_ptr  Pointer to volume index
 * @param param_2      Unknown parameter
 * @param info         Output: Mount info structure (44 bytes)
 * @param status       Output: Status code
 */

#include "disk.h"

/* Mount lock */
extern void *MOUNT_LOCK;

/* External functions */
extern void ML__EXCLUSION_START(void *lock);
extern void ML__EXCLUSION_STOP(void *lock);

/* Volume table offsets */
#define DISK_MOUNT_STATE_OFFSET   0x90
#define DISK_LV_DATA_OFFSET       0x84
#define DISK_LV_VOLX_OFFSET       0xb4
#define DISK_UID_HI_OFFSET        0x88
#define DISK_UID_LO_OFFSET        0x8c
#define DISK_DEV_INFO_OFFSET      0x94
#define DISK_UNIT_OFFSET          0x9a
#define DISK_SOMETHING_OFFSET     0xa2
#define DISK_SECTORS_OFFSET       0x9c
#define DISK_SECTOR_SIZE_OFFSET   0xa6
#define DISK_NUM_PARTS_OFFSET     0xa8
#define DISK_PART_TABLE_OFFSET    0x26
#define DISK_FLAGS_OFFSET         0xa5

/* Volume table base */
#define DISK_VOLUME_BASE  ((uint8_t *)0x00e7a1cc)

/* Valid volume index mask (volumes 1-10) */
#define VALID_VOL_MASK  0x7fe

/* Mount states */
#define DISK_MOUNT_ASSIGNED  2
#define DISK_MOUNT_MOUNTED   3

void DISK_$GET_MNT_INFO(uint16_t *vol_idx_ptr, void *param_2, void *info,
                         status_$t *status)
{
    uint16_t vol_idx;
    int32_t offset;
    uint8_t *vol_entry;
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

    ML__EXCLUSION_START(&MOUNT_LOCK);

    offset = (int16_t)(vol_idx * DISK_VOLUME_SIZE);
    vol_entry = DISK_VOLUME_BASE + offset;

    mount_state = *(uint16_t *)(vol_entry + DISK_MOUNT_STATE_OFFSET);

    if (mount_state != DISK_MOUNT_MOUNTED && mount_state != DISK_MOUNT_ASSIGNED) {
        *status = status_$volume_not_properly_mounted;
        ML__EXCLUSION_STOP(&MOUNT_LOCK);
        return;
    }

    /* Check for LV data and update vol_idx if needed */
    if (*(uint32_t *)(vol_entry + DISK_LV_DATA_OFFSET) == 0) {
        info_bytes[0x28] &= 0xbf;  /* Clear bit 6 */
    } else {
        info_bytes[0x28] |= 0x40;  /* Set bit 6 */
        vol_idx = *(uint16_t *)(vol_entry + DISK_LV_VOLX_OFFSET);
    }

    /* Recalculate offset with potentially new vol_idx */
    offset = (int16_t)(vol_idx * DISK_VOLUME_SIZE);
    vol_entry = DISK_VOLUME_BASE + offset;

    *status = status_$ok;

    /* Copy UID */
    *(uint32_t *)info_bytes = *(uint32_t *)(vol_entry + DISK_UID_HI_OFFSET);
    *(uint32_t *)(info_bytes + 4) = *(uint32_t *)(vol_entry + DISK_UID_LO_OFFSET);

    /* Get device info and copy type */
    dev_info = *(void **)(vol_entry + DISK_DEV_INFO_OFFSET);
    *(uint16_t *)(info_bytes + 8) = *(uint16_t *)((uint8_t *)dev_info + 4);
    *(uint16_t *)(info_bytes + 0x0a) = *(uint16_t *)(vol_entry + DISK_UNIT_OFFSET);
    *(uint16_t *)(info_bytes + 0x0c) = *(uint16_t *)(vol_entry + DISK_SOMETHING_OFFSET);
    *(uint32_t *)(info_bytes + 0x0e) = *(uint32_t *)(vol_entry + DISK_SECTORS_OFFSET);

    /* Encode sector size */
    sector_size_type = *(int16_t *)(vol_entry + DISK_SECTOR_SIZE_OFFSET);
    if (sector_size_type == 0) {
        *(uint16_t *)(info_bytes + 0x12) = 1;  /* 256 bytes */
    } else if (sector_size_type == 1) {
        *(uint16_t *)(info_bytes + 0x12) = 2;  /* 512 bytes */
    } else if (sector_size_type == 2) {
        *(uint16_t *)(info_bytes + 0x12) = 4;  /* 1024 bytes */
    }

    /* Copy partition count and misc info */
    *(uint16_t *)(info_bytes + 0x14) = *(uint16_t *)(vol_entry + DISK_NUM_PARTS_OFFSET);
    *(uint16_t *)(info_bytes + 0x26) = *(uint16_t *)(vol_entry + DISK_PART_TABLE_OFFSET + 0xb2);

    /* Clear partition info array */
    for (i = 0; i < 8; i++) {
        *(uint16_t *)(info_bytes + 0x16 + i * 2) = 0;
    }

    /* Fill partition info */
    num_parts = *(int16_t *)(vol_entry + DISK_NUM_PARTS_OFFSET) - 1;
    if (num_parts >= 0) {
        for (i = 0; i <= num_parts; i++) {
            /* This fills partition details - complex bit manipulation */
            /* TODO: Full implementation requires more reverse engineering */
        }
    }

    /* Set flags in byte at +0x28 */
    /* Bit 7: mounted flag (mount_state == 3) */
    mount_state = *(uint16_t *)(vol_entry + DISK_MOUNT_STATE_OFFSET);
    info_bytes[0x28] &= 0x7f;
    if (mount_state == DISK_MOUNT_MOUNTED) {
        info_bytes[0x28] |= 0x80;
    }

    /* Get device flags and set remaining bits */
    dev_info = *(void **)(vol_entry + DISK_DEV_INFO_OFFSET);
    dev_flags = *(uint16_t *)((uint8_t *)dev_info + 8);

    /* Bit 5: not negative flag */
    info_bytes[0x28] &= 0xdf;
    if ((int16_t)dev_flags >= 0) {
        info_bytes[0x28] |= 0x20;
    }

    /* Bit 4: write protect flag */
    info_bytes[0x28] &= 0xef;
    if ((vol_entry[DISK_FLAGS_OFFSET] & 1) != 0) {
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

    ML__EXCLUSION_STOP(&MOUNT_LOCK);
}
