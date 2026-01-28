/*
 * DISK_$LV_UID - Get the UID of a logical volume on a physical volume
 *
 * Reads the PV label to find the disk address of the specified logical
 * volume, then reads the LV label to extract its UID.
 *
 * Parameters:
 *   vol_idx    - Volume index of the physical volume
 *   lv_num     - Logical volume number (1-10)
 *   uid_ret    - Output: UID of the logical volume
 *   status_ret - Output: status code
 *
 * Original address: 0x00e6cc62
 * Original size: 336 bytes
 */

#include "disk/disk_internal.h"

/*
 * Volume table access
 *
 * Same base and entry layout as lv_mount.c. The volume table (DISK_$DVTBL)
 * is at 0xe7a290 with entries of 0x48 (72) bytes each.
 */
#define DISK_DVTBL_BASE       ((uint8_t *)0x00e7a290)
#define DISK_ENTRY_SIZE       0x48

/* Entry field offsets (from entry start) */
#define ENTRY_LV_BLOCK        0x08
#define ENTRY_DATA_ADDR       0x0C
#define ENTRY_MOUNT_STATE     0x14

/*
 * PV Label block offsets
 *
 * The PV label block has an LV block address table starting at offset 0x38.
 * Entries are indexed by lv_num (1-based), each 4 bytes (uint32_t).
 * lv_daddr = pv_label[0x38 + lv_num * 4]
 */
#define PV_LABEL_LV_BASE_OFFSET  0x38

/*
 * LV Label block offsets
 *
 * The LV label block contains the UID at offset 0x24:
 *   +0x24: UID high (uint32_t)
 *   +0x28: UID low (uint32_t)
 */
#define LV_LABEL_UID_HIGH_OFFSET  0x24
#define LV_LABEL_UID_LOW_OFFSET   0x28

void DISK_$LV_UID(int16_t vol_idx, int16_t lv_num, uid_t *uid_ret,
                  status_$t *status_ret)
{
    status_$t status;
    uint8_t *entry;
    uid_t lv_uid;
    uint32_t lv_daddr;
    void *block_ptr;
    uint8_t dummy[4]; /* local buffer passed as param_3 to DISK_$SET_BUFF */

    ML_$EXCLUSION_START(&MOUNT_LOCK);

    /* Calculate volume entry pointer */
    entry = DISK_DVTBL_BASE + (int16_t)(vol_idx * DISK_ENTRY_SIZE);

    /* Validate mount state == DISK_MOUNT_BUSY (3) */
    if (*(int16_t *)(entry + ENTRY_MOUNT_STATE) != DISK_MOUNT_BUSY) {
        status = status_$volume_not_properly_mounted;
        goto done;
    }

    /* Validate this is a physical volume (LV data offset == 0) */
    if (*(uint32_t *)(entry + ENTRY_LV_BLOCK) != 0) {
        status = status_$operation_requires_a_physical_volume;
        goto done;
    }

    /* Validate lv_num: must be 1-10 */
    if ((uint16_t)lv_num == 0 || (uint16_t)lv_num > MAX_LV_INDEX) {
        status = status_$invalid_logical_volume_index;
        goto done;
    }

    /* Read PV label block */
    block_ptr = DISK_$GET_BLOCK(vol_idx, 0, &PV_LABEL_$UID, 0, 0x20, &status);
    if (status != status_$ok && status != status_$storage_module_stopped) {
        goto done;
    }

    /* Extract LV block address from PV label */
    lv_daddr = *(uint32_t *)((uint8_t *)block_ptr +
                              PV_LABEL_LV_BASE_OFFSET +
                              (uint16_t)lv_num * 4);

    /* Release PV label buffer */
    DISK_$SET_BUFF(block_ptr, 0x08, dummy);

    /* Validate LV block address */
    if (lv_daddr == 0 || lv_daddr > *(uint32_t *)(entry + ENTRY_DATA_ADDR)) {
        status = status_$invalid_logical_volume_index;
        goto done;
    }

    /* Read LV label block */
    block_ptr = DISK_$GET_BLOCK(vol_idx, lv_daddr, &LV_LABEL_$UID, 0, 0x20, &status);
    if (status == status_$ok || status == status_$storage_module_stopped) {
        /* Extract UID from LV label */
        lv_uid.high = *(uint32_t *)((uint8_t *)block_ptr + LV_LABEL_UID_HIGH_OFFSET);
        lv_uid.low = *(uint32_t *)((uint8_t *)block_ptr + LV_LABEL_UID_LOW_OFFSET);

        /* Release LV label buffer */
        DISK_$SET_BUFF(block_ptr, 0x0c, dummy);
    }

done:
    ML_$EXCLUSION_STOP(&MOUNT_LOCK);
    uid_ret->high = lv_uid.high;
    uid_ret->low = lv_uid.low;
    *status_ret = status;
}
