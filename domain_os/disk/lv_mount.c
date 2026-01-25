/*
 * DISK_$LV_MOUNT - Mount a logical volume by UID
 *
 * Searches for a logical volume with the given UID across all mounted
 * physical volumes. When found, allocates a slot in the volume table
 * and populates it with the LV information.
 *
 * Original address: 0x00E6CA3A
 * Original size: 552 bytes
 */

#include "disk/disk_internal.h"

/*
 * Status codes for LV mount operations
 */
#define status_$disk_already_mounted       0x0008001e
#define status_$logical_volume_not_found   0x00080010

/*
 * Volume table access
 *
 * The volume table (DISK_$DVTBL) is at 0xe7a290 and contains entries
 * of 0x48 (72) bytes each. Indices 1-6 are for logical volumes,
 * while indices 1-10 are for physical volumes.
 *
 * Entry structure (offsets from entry base):
 *   +0x00 (-0x48): LV UID high
 *   +0x04 (-0x44): LV UID low
 *   +0x08 (-0x40): LV block number (0 for PV)
 *   +0x0C (-0x3C): Data address (data_start + reserved_blocks)
 *   +0x10 (-0x38): (other fields)
 *   +0x14 (-0x34): Mount state (0=free, 3=mounted)
 *   +0x18 (-0x30): Device info pointer
 *   +0x1C (-0x2C): Unit number
 *   +0x26 (-0x22): Shift value (log2 block size)
 *   +0x28 (-0x20): Flags
 */
#define DISK_DVTBL_BASE       ((uint8_t *)0x00e7a290)
#define DISK_ENTRY_SIZE       0x48

/* Entry field offsets (from entry start) */
#define ENTRY_UID_HIGH        0x00
#define ENTRY_UID_LOW         0x04
#define ENTRY_LV_BLOCK        0x08
#define ENTRY_DATA_ADDR       0x0C
#define ENTRY_MOUNT_STATE     0x14
#define ENTRY_SHIFT           0x26
#define ENTRY_FLAGS           0x28

/* Number of LV slots to scan (indices 1-6) */
#define LV_SLOT_COUNT         6

/* Number of PV entries to scan (indices 1-10) */
#define PV_SCAN_COUNT         10

/* Maximum LV entries per PV label */
#define MAX_LV_PER_PV         10

/*
 * PV Label block structure offsets
 *
 * The PV label block contains a table of LV block numbers at offset 0x3c.
 * Each entry is 4 bytes (block number), up to 10 entries.
 */
#define PV_LABEL_LV_TABLE_OFFSET   0x3c
#define PV_LABEL_LV_TABLE_SIZE     (MAX_LV_PER_PV * 4)

/*
 * LV Label block structure offsets
 *
 * The LV label block contains:
 *   +0x00: Version (word) - must be <= 1
 *   +0x24: LV UID (8 bytes)
 *   +0x2C: Reserved blocks count (long)
 *   +0x38: Data start block (long)
 *   +0x40: Shift value (word)
 */
#define LV_LABEL_VERSION_OFFSET     0x00
#define LV_LABEL_UID_OFFSET         0x24
#define LV_LABEL_RESERVED_OFFSET    0x2C
#define LV_LABEL_DATA_START_OFFSET  0x38
#define LV_LABEL_SHIFT_OFFSET       0x40

/* External UID constants */
extern uid_t PV_LABEL_$UID;
extern uid_t LV_LABEL_$UID;

/*
 * DISK_$LV_MOUNT - Mount a logical volume
 *
 * Searches all mounted physical volumes for a logical volume matching
 * the given UID. If found, allocates a slot in the LV portion of the
 * volume table and initializes it.
 *
 * Parameters:
 *   lv_uid     - Pointer to the UID of the logical volume to mount
 *   status_ret - Output: status code
 *
 * Returns:
 *   Volume index of the mounted LV, or 0 on error
 */
int16_t DISK_$LV_MOUNT(uid_t *lv_uid, status_$t *status_ret)
{
    uint32_t target_uid_high;
    uint32_t target_uid_low;
    int16_t free_slot;
    int16_t i, j;
    int16_t pv_idx;
    uint8_t *entry;
    uint8_t *pv_entry;
    uint16_t mount_state;
    uint32_t lv_block_table[MAX_LV_PER_PV];
    status_$t status;
    void *pv_label;
    void *lv_label;
    uint32_t lv_block;
    int32_t offset;

    /* Copy target UID to local variables */
    target_uid_high = lv_uid->high;
    target_uid_low = lv_uid->low;

    ML_$EXCLUSION_START(&MOUNT_LOCK);

    /*
     * Phase 1: Search for a free slot in the LV portion of the table,
     * while also checking if this LV is already mounted.
     *
     * Scan indices 6 down to 1 (LV slots).
     */
    free_slot = 0;
    entry = DISK_DVTBL_BASE + (LV_SLOT_COUNT * DISK_ENTRY_SIZE);

    for (i = LV_SLOT_COUNT; i >= 1; i--) {
        mount_state = *(uint16_t *)(entry + ENTRY_MOUNT_STATE);

        if (mount_state == 0) {
            /* Free slot found */
            free_slot = i;
        } else if (mount_state == DISK_MOUNT_MOUNTED) {
            /* Check if this LV is already mounted */
            if (*(uint32_t *)(entry + ENTRY_UID_HIGH) == target_uid_high &&
                *(uint32_t *)(entry + ENTRY_UID_LOW) == target_uid_low) {
                free_slot = i;
                status = status_$disk_already_mounted;
                goto done;
            }
        }

        entry -= DISK_ENTRY_SIZE;
    }

    /* Check if we found a free slot */
    if (free_slot == 0) {
        status = status_$volume_table_full;
        goto done;
    }

    /*
     * Phase 2: Search all mounted PVs for this LV.
     *
     * Scan indices 10 down to 1, looking for PV entries
     * (mount_state == 3, lv_block == 0).
     */
    pv_entry = DISK_DVTBL_BASE + (PV_SCAN_COUNT * DISK_ENTRY_SIZE);

    for (pv_idx = PV_SCAN_COUNT; pv_idx >= 1; pv_idx--) {
        mount_state = *(uint16_t *)(pv_entry + ENTRY_MOUNT_STATE);

        /* Check if this is a mounted PV (state == 3, lv_block == 0) */
        if (mount_state == DISK_MOUNT_MOUNTED &&
            *(uint32_t *)(pv_entry + ENTRY_LV_BLOCK) == 0) {

            /* Read the PV label block */
            pv_label = DISK_$GET_BLOCK(pv_idx, 0, &PV_LABEL_$UID, 0, 0, &status);
            if (status != status_$ok) {
                goto done;
            }

            /* Copy the LV block table from the PV label */
            uint32_t *src = (uint32_t *)((uint8_t *)pv_label + PV_LABEL_LV_TABLE_OFFSET);
            for (j = 0; j < MAX_LV_PER_PV; j++) {
                lv_block_table[j] = src[j];
            }

            /* Release the PV label buffer */
            DISK_$SET_BUFF(pv_label, 0x08, &status);

            /* Search through each LV entry on this PV */
            for (j = 0; j < MAX_LV_PER_PV; j++) {
                lv_block = lv_block_table[j];
                if (lv_block == 0) {
                    break;  /* No more LVs on this PV */
                }

                /* Read the LV label block */
                lv_label = DISK_$GET_BLOCK(pv_idx, lv_block, &LV_LABEL_$UID, 0, 0, &status);
                if (status != status_$ok) {
                    goto done;
                }

                /* Check LV version (<= 1) and UID match */
                int16_t version = *(int16_t *)((uint8_t *)lv_label + LV_LABEL_VERSION_OFFSET);
                uint32_t *lv_uid_ptr = (uint32_t *)((uint8_t *)lv_label + LV_LABEL_UID_OFFSET);

                if (version <= 1 &&
                    lv_uid_ptr[0] == target_uid_high &&
                    lv_uid_ptr[1] == target_uid_low) {

                    /*
                     * Found the LV! Copy PV entry to LV slot and update.
                     */
                    offset = free_slot * DISK_ENTRY_SIZE;
                    uint8_t *lv_entry = DISK_DVTBL_BASE + offset;

                    /* Copy entire PV entry as base */
                    uint32_t *dst = (uint32_t *)lv_entry;
                    uint32_t *pv_src = (uint32_t *)pv_entry;
                    for (int k = 0; k < (DISK_ENTRY_SIZE / 4); k++) {
                        dst[k] = pv_src[k];
                    }

                    /* Update with LV-specific data */
                    *(uint32_t *)(lv_entry + ENTRY_LV_BLOCK) = lv_block;

                    /* Data address = data_start + reserved_blocks */
                    uint32_t data_start = *(uint32_t *)((uint8_t *)lv_label + LV_LABEL_DATA_START_OFFSET);
                    uint32_t reserved = *(uint32_t *)((uint8_t *)lv_label + LV_LABEL_RESERVED_OFFSET);
                    *(uint32_t *)(lv_entry + ENTRY_DATA_ADDR) = data_start + reserved;

                    /* Set LV UID */
                    *(uint32_t *)(lv_entry + ENTRY_UID_HIGH) = target_uid_high;
                    *(uint32_t *)(lv_entry + ENTRY_UID_LOW) = target_uid_low;

                    /* Clear flags and set mount state */
                    *(uint16_t *)(lv_entry + ENTRY_FLAGS) = 0;
                    *(uint16_t *)(lv_entry + ENTRY_MOUNT_STATE) = DISK_MOUNT_MOUNTED;

                    /* Copy shift value from LV label */
                    uint16_t shift = *(uint16_t *)((uint8_t *)lv_label + LV_LABEL_SHIFT_OFFSET);
                    *(uint16_t *)(lv_entry + ENTRY_SHIFT) = shift;

                    /* Also update the PV entry's shift value */
                    *(uint16_t *)(pv_entry + ENTRY_SHIFT) = shift;

                    /* Release the LV label buffer */
                    DISK_$SET_BUFF(lv_label, 0x0c, &status);

                    status = status_$ok;
                    goto done;
                }

                /* Release the LV label buffer */
                DISK_$SET_BUFF(lv_label, 0x0c, &status);
            }
        }

        pv_entry -= DISK_ENTRY_SIZE;
    }

    /* LV not found on any PV */
    status = status_$logical_volume_not_found;

done:
    ML_$EXCLUSION_STOP(&MOUNT_LOCK);
    *status_ret = status;
    return free_slot;
}
