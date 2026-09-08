/*
 * DISK_$LV_MOUNT - Mount a logical volume by UID
 *
 * Searches for a logical volume with the given UID across all mounted
 * physical volumes.  When found, allocates a slot in the volume table
 * and populates it with the LV information.
 *
 * Original address: 0x00E6CA3A
 * Original size: 552 bytes
 */

#include "disk/disk_internal.h"

#include "bat/bat.h"    /* bat_$label_t: the logical-volume label record */

/*
 * Status codes for LV mount operations
 */

/*
 * The volume table is the array of disk_$volume_t descriptors DISK_VOL()
 * addresses.  The original walks it as `0xe7a290 + idx*0x48` with negative
 * displacements (-0x48 .. -0x20), which is the same cell as
 * `DISK_VOL(idx) + 0x00 .. 0x28`.
 */

/* Number of LV slots to scan (indices 1-6), 0x00E6CA64 `moveq #0x5,D0` */
#define LV_SLOT_COUNT         6

/* Number of PV entries to scan (indices 1-10), 0x00E6CAC6 `moveq #0x9,D4` */
#define PV_SCAN_COUNT         10

/* Maximum LV entries per PV label, 0x00E6CB2C `moveq #0x9,D0` */
#define MAX_LV_PER_PV         10

/*
 * PV label block: a table of ten LV block numbers at offset 0x3c
 * (0x00E6CB24 `lea (0x3c,A2),A0`).  The physical-volume label is the DISK
 * subsystem's own record; only this one field of it is used here.
 */
#define PV_LABEL_LV_TABLE_OFFSET   0x3c

/* PV_LABEL_$UID / LV_LABEL_$UID come from uid/uid.h via disk_internal.h */

/*
 * DISK_$SET_BUFF release codes (0x00E6CB3C, 0x00E6CC00, 0x00E6CC18)
 */
#define DISK_SET_BUFF_RELEASE_PV   0x08
#define DISK_SET_BUFF_RELEASE_LV   0x0c

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
    disk_$volume_t *entry;
    disk_$volume_t *pv_entry;
    disk_$volume_t *lv_entry;
    uint32_t lv_block_table[MAX_LV_PER_PV];
    status_$t status;
    /*
     * (-0x34,A6): the cell DISK_$SET_BUFF writes its status into.  The
     * original never reads it, and it is NOT the (-0x38,A6) cell that
     * carries the result out (0x00E6CB38, 0x00E6CBFC, 0x00E6CC14).
     */
    status_$t buf_status;
    void *pv_label;
    bat_$label_t *lv_label;
    uint32_t lv_block;

    /* 0x00E6CA48-0x00E6CA50: copy the target UID into the frame */
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
    entry = DISK_VOL(LV_SLOT_COUNT);

    for (i = LV_SLOT_COUNT; i >= 1; i--) {
        if (entry->mount_state == DISK_MOUNT_FREE) {
            /* 0x00E6CA80: free slot found */
            free_slot = i;
        } else if (entry->mount_state == DISK_MOUNT_MOUNTED) {
            /* 0x00E6CA96-0x00E6CA9C: is this LV already mounted? */
            if (entry->lv_uid.high == target_uid_high &&
                entry->lv_uid.low == target_uid_low) {
                free_slot = i;
                status = status_$disk_already_mounted;
                goto done;
            }
        }

        entry = (disk_$volume_t *)((uint8_t *)entry - DISK_VOLUME_SIZE);
    }

    /* 0x00E6CAB6: no free slot */
    if (free_slot == 0) {
        status = status_$volume_table_full;
        goto done;
    }

    /*
     * Phase 2: Search all mounted PVs for this LV.
     *
     * Scan indices 10 down to 1, looking for PV entries
     * (mount_state == 3, lv_start == 0).
     */
    pv_entry = DISK_VOL(PV_SCAN_COUNT);

    for (pv_idx = PV_SCAN_COUNT; pv_idx >= 1; pv_idx--) {
        /* 0x00E6CAEA-0x00E6CAF8 */
        if (pv_entry->mount_state == DISK_MOUNT_MOUNTED &&
            pv_entry->lv_start == 0) {

            /* 0x00E6CB10: read the PV label block */
            /*
             * 0x00E6CB02: one `clr.l` covers DISK_$GET_BLOCK's two
             * argument WORDS at (0x16,A6) and (0x18,A6) - block_type 0
             * and flags 0.
             */
            pv_label = DISK_$GET_BLOCK(pv_idx, 0, &PV_LABEL_$UID, 0, 0, 0,
                                       &status);
            if (status != status_$ok) {
                goto done;
            }

            /* 0x00E6CB24-0x00E6CB32: copy the LV block table out */
            {
                const uint32_t *src =
                    (const uint32_t *)((uint8_t *)pv_label +
                                       PV_LABEL_LV_TABLE_OFFSET);
                for (j = 0; j < MAX_LV_PER_PV; j++) {
                    lv_block_table[j] = src[j];
                }
            }

            /* 0x00E6CB42: release the PV label buffer */
            DISK_$SET_BUFF(pv_label, DISK_SET_BUFF_RELEASE_PV, &buf_status);

            /* Search through each LV entry on this PV */
            for (j = 0; j < MAX_LV_PER_PV; j++) {
                lv_block = lv_block_table[j];
                if (lv_block == 0) {
                    break;  /* 0x00E6CB58: no more LVs on this PV */
                }

                /* 0x00E6CB6C: read the LV label block */
                lv_label = (bat_$label_t *)
                    DISK_$GET_BLOCK(pv_idx, (int32_t)lv_block, &LV_LABEL_$UID,
                                    0, 0, 0, &status);   /* 0x00E6CB62 clr.l */
                if (status != status_$ok) {
                    goto done;
                }

                /*
                 * 0x00E6CB80: `cmpi.w #0x1,(A2)` / bgt -- accept version 1
                 * and below.  0x00E6CB88-0x00E6CB98: both halves of the UID.
                 */
                if (lv_label->version <= 1 &&
                    lv_label->lv_uid.high == target_uid_high &&
                    lv_label->lv_uid.low == target_uid_low) {

                    /* Found the LV: copy the PV descriptor into the LV slot */
                    lv_entry = DISK_VOL(free_slot);

                    /* 0x00E6CBBE: 18 longwords = the whole 0x48-byte record */
                    {
                        uint32_t *dst = (uint32_t *)lv_entry;
                        const uint32_t *pv_src = (const uint32_t *)pv_entry;
                        int16_t k;
                        for (k = 0; k < (DISK_VOLUME_SIZE / 4); k++) {
                            dst[k] = pv_src[k];
                        }
                    }

                    /* 0x00E6CBC4 */
                    lv_entry->lv_start = lv_block;

                    /*
                     * 0x00E6CBC8-0x00E6CBD0: the volume's address limit is
                     * the block the first BAT bit stands for plus the number
                     * of blocks the BAT represents.
                     */
                    lv_entry->addr_start = lv_label->first_data_block +
                                           lv_label->total_blocks;

                    /* 0x00E6CBD8-0x00E6CBDC */
                    lv_entry->lv_uid.high = target_uid_high;
                    lv_entry->lv_uid.low = target_uid_low;

                    /* 0x00E6CBE0, 0x00E6CBE4 */
                    lv_entry->as_options = 0;
                    lv_entry->mount_state = DISK_MOUNT_MOUNTED;

                    /* 0x00E6CBEA: the BAT step from the LV label */
                    lv_entry->bat_step = lv_label->bat_step;

                    /* 0x00E6CBF4: mirror it into the backing PV descriptor */
                    pv_entry->bat_step = lv_entry->bat_step;

                    /* 0x00E6CC06 */
                    DISK_$SET_BUFF(lv_label, DISK_SET_BUFF_RELEASE_LV,
                                   &buf_status);

                    status = status_$ok;
                    goto done;
                }

                /* 0x00E6CC1E */
                DISK_$SET_BUFF(lv_label, DISK_SET_BUFF_RELEASE_LV,
                               &buf_status);
            }
        }

        pv_entry = (disk_$volume_t *)((uint8_t *)pv_entry - DISK_VOLUME_SIZE);
    }

    /* 0x00E6CC3A: LV not found on any PV */
    status = status_$logical_volume_not_found;

done:
    ML_$EXCLUSION_STOP(&MOUNT_LOCK);
    *status_ret = status;
    return free_slot;
}
