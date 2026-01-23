/*
 * DISK_$LV_ASSIGN - Logical Volume Assignment
 *
 * Assigns a logical volume from a physical volume. The physical volume
 * must already be assigned (mounted) to the current process.
 *
 * This syscall reads the PV label block (daddr=0) to get the logical
 * volume map, then assigns the requested logical volume to a free slot
 * in the volume table.
 *
 * Parameters:
 *   vol_idx_ptr       - Pointer to physical volume index (1-10)
 *   lv_idx_ptr        - Pointer to logical volume index (1-10)
 *   blocks_avail_ptr  - Output: number of blocks available in LV
 *   status            - Output: status code
 *
 * Returns:
 *   Volume index of the newly assigned logical volume (0 if failed)
 *
 * Error conditions:
 *   status_$invalid_volume_index - vol_idx not in range 1-10
 *   status_$invalid_logical_volume_index - lv_idx not in range 1-10 or
 *                                          LV start address is invalid
 *   status_$volume_not_properly_mounted - PV not assigned to current process
 *   status_$operation_requires_a_physical_volume - volume is an LV, not PV
 *   status_$volume_in_use - LV already assigned
 *   status_$volume_table_full - no free volume table slots
 *
 * Original address: 0x00e6cdb2
 */

#include "disk/disk_internal.h"

/*
 * PV Label block layout (read from daddr=0):
 *   +0x28: UID high (uint32_t)
 *   +0x2c: UID low (uint32_t)
 *   +0x38: LV start addresses array[10] (uint32_t each)
 *   +0x60: LV end addresses array[10] (uint32_t each)
 *
 * The LV start address for index i is at offset 0x38 + i*4
 * The LV end address for index i is at offset 0x60 + i*4
 */
#define PVLABEL_UID_HIGH_OFFSET    0x28
#define PVLABEL_UID_LOW_OFFSET     0x2c
#define PVLABEL_LV_START_OFFSET    0x38
#define PVLABEL_LV_END_OFFSET      0x60

uint16_t DISK_$LV_ASSIGN(uint16_t *vol_idx_ptr, uint16_t *lv_idx_ptr,
                         int32_t *blocks_avail_ptr, status_$t *status)
{
    uint16_t vol_idx;
    uint16_t lv_idx;
    int32_t offset;
    uint8_t *vol_entry;
    uint16_t mount_state;
    int16_t mount_proc;
    int16_t saved_mount_state;
    int8_t access_granted;
    void *pv_block;
    status_$t local_status;

    /* LV info from PV label */
    uint32_t lv_start_addr;
    int32_t lv_end_addr;
    int32_t lv_size;

    /* Volume table search */
    uint16_t free_slot;
    int16_t scan_idx;
    uint8_t *scan_entry;
    uint16_t scan_mount_state;
    uint32_t scan_lv_data;

    /* For copying volume entry */
    int32_t src_offset;
    int32_t dest_offset;
    uint8_t *src_entry;
    uint8_t *dest_entry;
    int16_t copy_idx;

    uint16_t result_vol_idx = 0;

    /* Read input parameters */
    vol_idx = *vol_idx_ptr;
    lv_idx = *lv_idx_ptr;

    /* Validate volume index (must be 1-10) */
    if ((((uint32_t)1 << (vol_idx & 0x1f)) & VALID_VOL_MASK) == 0) {
        *status = status_$invalid_volume_index;
        return 0;
    }

    /* Validate logical volume index (must be 1-10, not 0) */
    if (lv_idx > MAX_LV_INDEX || lv_idx == 0) {
        *status = status_$invalid_logical_volume_index;
        return 0;
    }

    /* Set up cleanup handler */
    PROC2_$SET_CLEANUP(5);

    /* Acquire mount lock */
    ML_$EXCLUSION_START(&MOUNT_LOCK);

    /* Get volume entry */
    offset = (int16_t)(vol_idx * DISK_VOLUME_SIZE);
    vol_entry = DISK_VOLUME_BASE + offset;

    /* Check mount state and ownership */
    mount_state = *(uint16_t *)(vol_entry + DISK_MOUNT_STATE_OFFSET);
    mount_proc = *(int16_t *)(vol_entry + DISK_MOUNT_PROC_OFFSET);
    saved_mount_state = mount_state;

    /* Determine if access is granted:
     * - mount_state == 3 (BUSY) grants access
     * - mount_state == 2 (ASSIGNED) and mount_proc == current process grants access
     */
    access_granted = 0;
    if (mount_state == DISK_MOUNT_BUSY) {
        access_granted = -1;
    } else if (mount_state == DISK_MOUNT_ASSIGNED && mount_proc == PROC1_$CURRENT) {
        access_granted = -1;
    }

    if (!access_granted) {
        local_status = status_$volume_not_properly_mounted;
        goto done;
    }

    /* Must be a physical volume (LV data must be 0) */
    if (*(uint32_t *)(vol_entry + DISK_LV_DATA_OFFSET) != 0) {
        local_status = status_$operation_requires_a_physical_volume;
        goto done;
    }

    /* Set mount state to BUSY while we work */
    *(uint16_t *)(vol_entry + DISK_MOUNT_STATE_OFFSET) = DISK_MOUNT_BUSY;

    /* Read PV label block (daddr=0) to get LV map */
    pv_block = DISK_$GET_BLOCK(vol_idx, 0, PV_LABEL__UID, 0, 0, &local_status);
    if (local_status != status_$ok) {
        goto done;
    }

    /* Get LV start address from PV label */
    lv_start_addr = *(uint32_t *)((uint8_t *)pv_block + PVLABEL_LV_START_OFFSET +
                                   lv_idx * sizeof(uint32_t));

    /* Get LV end address (if present) */
    lv_end_addr = *(int32_t *)((uint8_t *)pv_block + PVLABEL_LV_END_OFFSET +
                                lv_idx * sizeof(uint32_t));
    if (lv_end_addr != 0) {
        lv_end_addr = lv_end_addr - lv_start_addr;
    }

    /* Calculate LV size: either from next LV start or from PV end */
    if (lv_idx < 10 &&
        *(int32_t *)((uint8_t *)pv_block + PVLABEL_LV_START_OFFSET +
                     (lv_idx + 1) * sizeof(uint32_t)) != 0) {
        /* Use next LV's start address */
        lv_size = *(int32_t *)((uint8_t *)pv_block + PVLABEL_LV_START_OFFSET +
                               (lv_idx + 1) * sizeof(uint32_t)) - lv_start_addr;
    } else {
        /* Use PV's end address */
        lv_size = *(int32_t *)(vol_entry + DISK_ADDR_END_OFFSET) - lv_start_addr;
    }

    /* Release PV label block */
    DISK_$SET_BUFF(pv_block, 0x0c, NULL);

    /* Validate LV start address */
    if (lv_start_addr == 0 ||
        lv_start_addr > *(uint32_t *)(vol_entry + DISK_ADDR_END_OFFSET)) {
        local_status = status_$invalid_logical_volume_index;
        goto done;
    }

    /* Search for free slot and check for duplicates */
    free_slot = 0;
    for (scan_idx = VOL_TABLE_SCAN_COUNT; scan_idx >= 1; scan_idx--) {
        scan_entry = DISK_VOLUME_BASE + (scan_idx * DISK_VOLUME_SIZE);
        scan_mount_state = *(uint16_t *)(scan_entry + DISK_MOUNT_STATE_OFFSET);

        if (scan_mount_state == DISK_MOUNT_FREE) {
            /* Found a free slot */
            free_slot = scan_idx;
        } else {
            /* Check for duplicate: same PV UID and same LV start address */
            if (*(uint16_t *)(scan_entry + DISK_DEVICE_UNIT_OFFSET) ==
                *(uint16_t *)(vol_entry + DISK_DEVICE_UNIT_OFFSET) &&
                *(uint32_t *)(scan_entry + DISK_LV_DATA_OFFSET) == lv_start_addr &&
                *(int32_t *)(scan_entry + DISK_UID_LOW_OFFSET) ==
                *(int32_t *)(vol_entry + DISK_UID_LOW_OFFSET)) {
                local_status = status_$volume_in_use;
                goto done;
            }
        }
    }

    if (free_slot == 0) {
        local_status = status_$volume_table_full;
        goto done;
    }

    /* Copy PV entry to new LV slot */
    src_offset = (int16_t)(vol_idx * DISK_VOLUME_SIZE);
    dest_offset = (int16_t)(free_slot * DISK_VOLUME_SIZE);
    src_entry = DISK_VOLUME_BASE + src_offset;
    dest_entry = DISK_VOLUME_BASE + dest_offset;

    /* Copy entire entry (18 uint32_t = 72 bytes) */
    for (copy_idx = 0; copy_idx < 18; copy_idx++) {
        ((uint32_t *)dest_entry)[copy_idx] = ((uint32_t *)src_entry)[copy_idx];
    }

    /* Set LV-specific fields in new entry */
    *(uint32_t *)(dest_entry + DISK_LV_DATA_OFFSET) = lv_start_addr;
    *(int32_t *)(dest_entry + DISK_ADDR_END_OFFSET) = lv_size;
    *(int16_t *)(dest_entry + DISK_MOUNT_PROC_OFFSET) = PROC1_$CURRENT;
    *(uint16_t *)(dest_entry + DISK_VOL_INFO2_OFFSET) = 0;  /* Clear reserved field */
    *(uint16_t *)(dest_entry + DISK_MOUNT_STATE_OFFSET) = DISK_MOUNT_ASSIGNED;

    result_vol_idx = free_slot;
    local_status = status_$ok;

done:
    /* Restore original mount state for PV */
    *(uint16_t *)(vol_entry + DISK_MOUNT_STATE_OFFSET) = saved_mount_state;

    /* Release mount lock */
    ML_$EXCLUSION_STOP(&MOUNT_LOCK);

    /* Return available blocks */
    *blocks_avail_ptr = lv_end_addr;

    *status = local_status;
    return result_vol_idx;
}
