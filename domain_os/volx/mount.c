/*
 * VOLX_$MOUNT - Mount a logical volume
 *
 * Coordinates mounting a volume by calling the DISK, VTOC, and DIR
 * subsystems. On success, populates the VOLX table entry with the
 * volume's UIDs and device location.
 *
 * Original address: 0x00E6B118
 */

#include "volx/volx_internal.h"

/*
 * VOLX_$MOUNT
 *
 * Parameters:
 *   dev         - Pointer to device unit number
 *   bus         - Pointer to bus/controller number
 *   ctlr        - Pointer to controller type
 *   lv_num      - Pointer to logical volume number
 *   salvage_ok  - Pointer to salvage flag (passed to VTOC_$MOUNT)
 *   write_prot  - Pointer to write protect flag (negative = write protected)
 *   parent_uid  - Pointer to parent directory UID (or UID_$NIL for no mount)
 *   dir_uid_ret - Output: receives root directory UID
 *   status      - Output: status code
 *
 * Algorithm:
 *   1. Mount the physical volume via DISK_$PV_MOUNT
 *   2. Get the logical volume UID via DISK_$LV_UID
 *   3. Mount the logical volume via DISK_$LV_MOUNT
 *   4. Build the mount parameter from device location fields
 *   5. Mount the VTOC via VTOC_$MOUNT
 *   6. Get the name directories via VTOC_$GET_NAME_DIRS
 *   7. If parent_uid is not nil, add mount point via DIR_$ADD_MOUNT
 *   8. Store volume info in VOLX table entry
 *
 * Notes:
 *   - If DISK_$PV_MOUNT returns status_$disk_already_mounted, we continue
 *   - On error after VTOC_$MOUNT, we dismount the VTOC and DISK
 *   - The mount parameter is a packed word containing dev/bus/ctlr/lv_num
 */
void VOLX_$MOUNT(int16_t *dev, int16_t *bus, int16_t *ctlr, int16_t *lv_num,
                 int8_t *salvage_ok, int8_t *write_prot, uid_t *parent_uid,
                 uid_t *dir_uid_ret, status_$t *status)
{
    int16_t dev_val, bus_val, ctlr_val, lv_num_val;
    int8_t salvage_val, write_prot_val;
    uid_t parent_uid_val;
    int16_t pv_idx;
    int16_t vol_idx;
    boolean already_mounted;
    status_$t local_status;
    status_$t vtoc_status;
    uid_t lv_uid;
    uid_t name_dir1_uid;
    uid_t dir_uid;
    uint16_t mount_param;

    /* Read input parameters */
    dev_val = *dev;
    bus_val = *bus;
    ctlr_val = *ctlr;
    lv_num_val = *lv_num;
    salvage_val = *salvage_ok;
    write_prot_val = *write_prot;
    parent_uid_val = *parent_uid;

    /* Mount the physical volume */
    pv_idx = DISK_$PV_MOUNT(dev_val, bus_val, ctlr_val, &local_status);

    already_mounted = (local_status == status_$disk_already_mounted);

    if (!already_mounted && local_status != status_$ok) {
        goto error_return;
    }

    /* Get the logical volume UID */
    DISK_$LV_UID(pv_idx, lv_num_val, &lv_uid, &local_status);
    if (local_status != status_$ok) {
        goto dismount_pv;
    }

    /* Mount the logical volume */
    vol_idx = DISK_$LV_MOUNT(&lv_uid, &local_status);
    if (local_status != status_$ok) {
        goto dismount_pv;
    }

    /*
     * Build mount parameter - packed device location
     * High byte: (dev << 3) | (bus & 0x07)
     * Low byte:  (ctlr << 4) | (lv_num & 0x0F)
     */
    mount_param = (((uint8_t)dev_val << 3) | ((uint8_t)bus_val & 0x07)) << 8;
    mount_param |= ((uint8_t)ctlr_val << 4) | ((uint8_t)lv_num_val & 0x0F);

    /* Mount the VTOC */
    VTOC_$MOUNT(vol_idx, mount_param, salvage_val, write_prot_val, &vtoc_status);

    if (vtoc_status != status_$ok) {
        if (vtoc_status != status_$disk_write_protected) {
            DISK_$DISMOUNT(vol_idx);
            goto dismount_pv;
        }
        /* Write protected - check if caller allows it */
        if (write_prot_val < 0) {
            vtoc_status = status_$ok;
        } else {
            /* Return a different status for write-protected volume */
            vtoc_status = 0x14ffff;  /* TODO: Identify this status code */
        }
    }

    /* Get the name directories */
    VTOC_$GET_NAME_DIRS(vol_idx, &name_dir1_uid, &dir_uid, &local_status);
    if (local_status != status_$ok) {
        goto dismount_vtoc;
    }

    /* Return the directory UID */
    *dir_uid_ret = dir_uid;

    /* If parent UID is not nil, add mount point */
    if (parent_uid_val.high != UID_$NIL.high ||
        parent_uid_val.low != UID_$NIL.low) {
        DIR_$ADD_MOUNT(&parent_uid_val, &dir_uid, &local_status);

        if (local_status == status_$directory_is_full) {
            local_status = status_$stream_cant_stream_this_object_type;
            goto dismount_vtoc;
        }
        if (local_status == status_$name_already_exists) {
            local_status = status_$stream_no_more_streams;
        }
        if (local_status != status_$ok) {
            goto dismount_vtoc;
        }
    }

    /* Store volume info in VOLX table */
    {
        volx_entry_t *entry = &VOLX_$TABLE_BASE[vol_idx];

        entry->dir_uid = dir_uid;
        entry->lv_uid = lv_uid;
        entry->parent_uid = parent_uid_val;
        entry->dev = dev_val;
        entry->bus = bus_val;
        entry->ctlr = ctlr_val;
        entry->lv_num = lv_num_val;
    }

    /* Success - return vtoc_status (may indicate write-protected) */
    *status = vtoc_status;
    return;

dismount_vtoc:
    VTOC_$DISMOUNT(vol_idx, 0, &vtoc_status);
    /* Fall through to dismount_pv */

dismount_pv:
    if (!already_mounted) {
        DISK_$DISMOUNT(pv_idx);
    }
    /* Fall through to error_return */

error_return:
    *status = local_status;
}
