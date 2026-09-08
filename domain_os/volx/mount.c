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
 *
 * Which cell reaches the caller (0x00E6B32C-0x00E6B33A) is decided by
 * local_status (A6-0x2C) alone: when it is zero the caller gets vtoc_status
 * (A6-0x24).  Every path that only VTOC_$MOUNT failed on leaves local_status
 * at zero, so those failures ARE reported - see the dismount_lv label.
 * (source-rurk)
 */
void VOLX_$MOUNT(int16_t *dev, int16_t *bus, int16_t *ctlr, int16_t *lv_num,
                 int8_t *salvage_ok, int8_t *write_prot, uid_t *parent_uid,
                 uid_t *dir_uid_ret, status_$t *status)
{
    int16_t dev_val, bus_val, ctlr_val, lv_num_val;   /* D4, D5, D6, D7 */
    int8_t salvage_val;                               /* A6-0x40 */
    int8_t write_prot_val;                            /* D3 */
    uid_t parent_uid_val;                             /* A6-0x20 */
    int16_t pv_idx;                                   /* A6-0x32 */
    int16_t vol_idx;                                  /* D2 */
    boolean already_mounted;                          /* A6-0x3C */
    status_$t local_status;                           /* A6-0x2C */
    status_$t vtoc_status;                            /* A6-0x24 */
    status_$t dismount_status;                        /* A6-0x28 */
    uid_t lv_uid;                                     /* A6-0x18 */
    uid_t name_dir_uid;                               /* A6-0x10 */
    uid_t dir_uid;                                    /* A6-0x08 */
    uint8_t mp_hi, mp_lo;                             /* A6-0x2E, A6-0x2D */
    uint16_t mount_param;

    /* 0x00E6B12A-0x00E6B15A: every parameter is a var parameter; the values
     * are pulled into registers/locals once, up front. */
    dev_val        = *dev;
    bus_val        = *bus;
    ctlr_val       = *ctlr;
    lv_num_val     = *lv_num;
    salvage_val    = *salvage_ok;
    write_prot_val = *write_prot;
    parent_uid_val = *parent_uid;

    /* 0x00E6B15C-0x00E6B172: mount the physical volume. */
    pv_idx = DISK_$PV_MOUNT(dev_val, bus_val, ctlr_val, &local_status);

    /*
     * 0x00E6B176-0x00E6B18C:
     *   cmpi.l #0x8001e,(-0x2c,A6) / seq D1b / move.b D1b,(-0x3c,A6) / bmi
     * `seq` makes a Domain boolean (0xFF/0x00) and the `bmi` tests the byte
     * just stored, so "already mounted" short-circuits the error test below.
     */
    already_mounted = (local_status == status_$disk_already_mounted) ? -1 : 0;
    if (already_mounted >= 0) {
        if (local_status != status_$ok) {
            goto report_local_status;       /* 0x00E6B18A -> 0x00E6B338 */
        }
    }

    /* 0x00E6B18E-0x00E6B1A8 */
    DISK_$LV_UID(pv_idx, lv_num_val, &lv_uid, &local_status);
    if (local_status != status_$ok) {
        goto dismount_pv;                   /* -> 0x00E6B318 */
    }

    /* 0x00E6B1AC-0x00E6B1C2 */
    vol_idx = DISK_$LV_MOUNT(&lv_uid, &local_status);
    if (local_status != status_$ok) {
        goto dismount_pv;                   /* -> 0x00E6B318 */
    }

    /*
     * 0x00E6B1C6-0x00E6B1F8: Pascal packed-field stores into the two bytes of
     * the mount-parameter word at A6-0x2E / A6-0x2D.  Each field is written as
     * "AND keeping the other field's bits, OR the new value"; the second AND of
     * each pair then clears exactly the bits the first AND preserved, so both
     * reads of the uninitialised local are dead and the result is fully
     * determined.  Reproduced instruction for instruction:
     *
     *   andi.b #0x07,(-0x2e,A6)                     (dead read)
     *   move.b D4b,D1b / lsl.b #0x3,D1b / or.b D1b,(-0x2e,A6)
     *   andi.b #0xF8,(-0x2e,A6)
     *   move.b D5b,D1b / or.b D1b,(-0x2e,A6)        bus, UNMASKED
     *
     * and the same shape for ctlr/lv_num with #0x0F / #0xF0.  The image applies
     * no &7 to bus and no &0xF to lv_num, so an out-of-range bus or lv_num
     * corrupts the neighbouring field here exactly as it does in the original.
     * (source-rurk)
     */
    mp_hi = 0;      /* uninitialised in the image; every read bit is masked off */
    mp_lo = 0;
    mp_hi &= 0x07;                                          /* 0x00E6B1C6 */
    mp_hi |= (uint8_t)((uint8_t)dev_val << 3);              /* 0x00E6B1CC */
    mp_hi &= 0xF8;                                          /* 0x00E6B1D4 */
    mp_hi |= (uint8_t)bus_val;                              /* 0x00E6B1DA */
    mp_lo &= 0x0F;                                          /* 0x00E6B1E0 */
    mp_lo |= (uint8_t)((uint8_t)ctlr_val << 4);             /* 0x00E6B1E6 */
    mp_lo &= 0xF0;                                          /* 0x00E6B1EE */
    mp_lo |= (uint8_t)lv_num_val;                           /* 0x00E6B1F4 */

    /* 0x00E6B204 `move.w (-0x2e,A6),-(SP)` pushes the pair as one big-endian
     * word - A6-0x2E is the high byte.  Build it with shifts so a
     * little-endian host produces the same value. */
    mount_param = (uint16_t)(((uint16_t)mp_hi << 8) | (uint16_t)mp_lo);

    /* 0x00E6B1FA-0x00E6B218: VTOC_$MOUNT reports into its OWN status cell. */
    VTOC_$MOUNT(vol_idx, mount_param, salvage_val, write_prot_val, &vtoc_status);

    if (vtoc_status != status_$ok) {
        if (vtoc_status != status_$disk_write_protected) {
            /* 0x00E6B220 bne.w 0x00E6B30C - dismount the logical volume and
             * fall into the common tail, where local_status is still 0 so the
             * caller is handed vtoc_status.  (source-rurk) */
            goto dismount_lv;
        }
        /* 0x00E6B224-0x00E6B234: the caller asked for a read-only mount, or
         * is told about it with the 0xFFFF warning subcode. */
        if (write_prot_val < 0) {
            vtoc_status = status_$ok;                       /* 0x00E6B228 */
        } else {
            vtoc_status = status_$volume_disk_is_write_protected;  /* 0x00E6B22E */
        }
    }

    /* 0x00E6B236-0x00E6B256 */
    VTOC_$GET_NAME_DIRS(vol_idx, &name_dir_uid, &dir_uid, &local_status);
    if (local_status != status_$ok) {
        goto dismount_vtoc;                 /* -> 0x00E6B2FA */
    }

    /* 0x00E6B258-0x00E6B264 */
    *dir_uid_ret = dir_uid;

    /* 0x00E6B266-0x00E6B278: cmpm.l pair against UID_$NIL */
    if (parent_uid_val.high != UID_$NIL.high ||
        parent_uid_val.low  != UID_$NIL.low) {
        /* 0x00E6B27A-0x00E6B28C */
        DIR_$ADD_MOUNT(&parent_uid_val, &dir_uid, &local_status);

        /* 0x00E6B290-0x00E6B2B4: two DIR_ codes are re-mapped into module 0x14.
         * The `bra.b 0x00E6B2BA` at 0x00E6B2A2 skips the `tst.l`, but the flags
         * still come from the `move.l #0x140006`, which is non-zero, so both
         * re-mapped codes reach the dismount path. */
        if (local_status == status_$directory_is_full) {
            local_status = status_$volx_volume_table_full;
            goto dismount_vtoc;
        }
        if (local_status == status_$name_already_exists) {
            local_status = status_$volx_directory_in_use;
        }
        if (local_status != status_$ok) {   /* 0x00E6B2B6-0x00E6B2BA */
            goto dismount_vtoc;
        }
    }

    /* 0x00E6B2BC-0x00E6B2F6: populate the (1-based) VOLX table entry. */
    {
        volx_$entry_t *entry = VOLX_$ENTRY(vol_idx);

        entry->dir_uid    = dir_uid;
        entry->lv_uid     = lv_uid;
        entry->parent_uid = parent_uid_val;
        entry->dev        = dev_val;
        entry->bus        = bus_val;
        entry->ctlr       = ctlr_val;
        entry->lv_num     = lv_num_val;
    }

    goto report;                            /* 0x00E6B2F8 bra.b 0x00E6B32C */

dismount_vtoc:                              /* 0x00E6B2FA */
    /* The dismount reports into its own cell at A6-0x28, which is never read;
     * it must NOT overwrite vtoc_status (A6-0x24).  (source-rurk) */
    VTOC_$DISMOUNT(vol_idx, 0, &dismount_status);
    goto dismount_pv;                       /* 0x00E6B30A bra.b 0x00E6B318 */

dismount_lv:                                /* 0x00E6B30C */
    DISK_$DISMOUNT(vol_idx);
    /* falls through to 0x00E6B318 */

dismount_pv:                                /* 0x00E6B318 */
    if (already_mounted >= 0) {
        DISK_$DISMOUNT(pv_idx);
    }
    /* falls through to 0x00E6B32C */

report:                                     /* 0x00E6B32C */
    if (local_status != status_$ok) {
        goto report_local_status;
    }
    *status = vtoc_status;                  /* 0x00E6B332 */
    return;

report_local_status:                        /* 0x00E6B338 */
    *status = local_status;
}
