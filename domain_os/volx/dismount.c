/*
 * VOLX_$DISMOUNT - Dismount a logical volume
 *
 * Dismounts a volume by its physical location. Validates that the volume
 * is still at the expected location, removes the mount point link, and
 * calls AST_$DISMOUNT to flush cached data.
 *
 * Original address: 0x00E6B346
 */

#include "volx/volx_internal.h"

/*
 * VOLX_$DISMOUNT
 *
 * Parameters:
 *   dev         - Pointer to device unit number
 *   bus         - Pointer to bus/controller number
 *   ctlr        - Pointer to controller type
 *   lv_num      - Pointer to logical volume number
 *   entry_uid   - Pointer to expected entry UID (or UID_$NIL to skip check)
 *   force       - Pointer to force flag (negative = skip disk change check)
 *   status      - Output: status code
 *
 * Algorithm:
 *   1. If diskless, return not mounted
 *   2. Find volume in VOLX table by device location
 *   3. Remount physical volume to check if disk changed
 *   4. Verify logical volume UID still matches (unless force flag set)
 *   5. Check not dismounting boot volume
 *   6. Validate entry_uid if provided
 *   7. Remove mount point via DIR_$DROP_MOUNT if parent_uid set
 *   8. Call AST_$DISMOUNT to flush and invalidate
 *   9. Clear the VOLX table entry
 */
void VOLX_$DISMOUNT(int16_t *dev, int16_t *bus, int16_t *ctlr, int16_t *lv_num,
                    uid_t *entry_uid, int8_t *force, status_$t *status)
{
    int16_t dev_val, bus_val, ctlr_val, lv_num_val;
    int8_t force_val;
    uid_t entry_uid_val;
    int16_t vol_idx;
    int16_t pv_idx;
    status_$t local_status;
    status_$t dummy_status;
    uid_t lv_uid;
    volx_entry_t *entry;

    /* Read input parameters */
    dev_val = *dev;
    bus_val = *bus;
    ctlr_val = *ctlr;
    lv_num_val = *lv_num;
    entry_uid_val = *entry_uid;
    force_val = *force;

    /* Check if really diskless */
    if (NETWORK_$REALLY_DISKLESS >= 0) {
        *status = status_$volume_logical_vol_not_mounted;
        return;
    }

    /* Find volume in table */
    vol_idx = FIND_VOLX(dev_val, bus_val, ctlr_val, lv_num_val);
    if (vol_idx == 0) {
        *status = status_$volume_logical_vol_not_mounted;
        return;
    }

    /* Try to remount physical volume to check if disk changed */
    pv_idx = DISK_$PV_MOUNT(dev_val, bus_val, ctlr_val, &local_status);

    if (local_status == status_$ok) {
        /* Disk was not mounted - dismount and return error */
        DISK_$DISMOUNT(pv_idx);
        *status = status_$volume_logical_vol_not_mounted;
        return;
    }

    if (local_status != status_$disk_already_mounted) {
        *status = local_status;
        return;
    }

    /* Get current logical volume UID */
    DISK_$LV_UID(pv_idx, lv_num_val, &lv_uid, &local_status);

    entry = &VOLX_$TABLE_BASE[vol_idx];

    if (local_status == status_$storage_module_stopped) {
        /* Check if LV UID still matches */
        if (lv_uid.high == entry->lv_uid.high &&
            lv_uid.low == entry->lv_uid.low) {
            /* Revalidate the disk */
            DISK_$REVALIDATE(vol_idx);
        } else if (force_val >= 0) {
            /* Disk replaced and not forcing - error */
            *status = status_$volume_physical_vol_replaced_since_mount;
            return;
        }
    }

    /* Cannot dismount boot volume */
    if (vol_idx == CAL_$BOOT_VOLX) {
        *status = status_$volume_unable_to_dismount_boot_volume;
        return;
    }

    /* Validate entry_uid if not nil */
    if (entry_uid_val.high != UID_$NIL.high ||
        entry_uid_val.low != UID_$NIL.low) {
        if (entry_uid_val.high != entry->dir_uid.high ||
            entry_uid_val.low != entry->dir_uid.low) {
            *status = status_$volume_entry_directory_not_on_logical_volume;
            return;
        }
    }

    /* Remove mount point if parent_uid is set */
    if (entry->parent_uid.high != UID_$NIL.high ||
        entry->parent_uid.low != UID_$NIL.low) {
        static uint32_t unused_param = 0;

        DIR_$DROP_MOUNT(&entry->parent_uid, &entry_uid_val, &unused_param,
                        &local_status);
        if (local_status != status_$ok) {
            *status = local_status;
            return;
        }

        /* Clear parent_uid */
        entry->parent_uid = UID_$NIL;
    }

    /* Call AST_$DISMOUNT to flush and invalidate */
    AST_$DISMOUNT(vol_idx, force_val, &local_status);

    if (local_status == status_$ok) {
        /* Clear the lv_num field to mark entry as unused */
        entry->lv_num = 0;
    }

    *status = local_status;
}
