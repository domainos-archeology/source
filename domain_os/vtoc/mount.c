/*
 * VTOC_$MOUNT - Mount a volume's VTOC
 *
 * Original address: 0x00e38584
 * Size: 478 bytes
 *
 * Initializes the VTOC subsystem for a volume after BAT_$MOUNT.
 * Reads the volume label block and copies VTOC configuration data.
 */

#include "vtoc/vtoc_internal.h"
#include "audit/audit.h"

/* 0x00E38762: the constant length word AUDIT_$LOG_EVENT is handed by
 * reference (`pea (0x22,PC)` at 0x00E3873E; PC = 0x00E38740). */
static const uint16_t vtoc_$audit_mount_len = 0x32;

void VTOC_$MOUNT(int16_t vol_idx, uint16_t param_2, uint8_t param_3, char param_4,
                 status_$t *status_ret)
{
    int16_t i;
    int16_t vol_offset;
    uint32_t *label_block;
    uint32_t *dst;
    uint32_t *src;
    status_$t bat_status;
    status_$t local_status;
    /*
     * A6-0x38: the 0x32-byte audit event record.  vol_uid and the trailing
     * fields are part of the SAME record as the name, not separate locals -
     * 0x00E38658 writes A6-0x14 = record+0x24.
     */
    vtoc_$audit_mount_rec_t audit_rec;

    /* If param_4 negative, set write protection */
    if (param_4 < 0) {
        DISK_$WRITE_PROTECT(0, vol_idx, &local_status);
    }

    *status_ret = status_$ok;

    /* Mount the BAT first */
    BAT_$MOUNT(vol_idx, param_3, &bat_status);

    /* Clear mount status */
    vtoc_$data.mounted[vol_idx] = 0;

    /* Continue only if BAT mount succeeded or returned write protected */
    if (bat_status != status_$ok && bat_status != 0x80007 /* status_$disk_write_protected */) {
        goto done;
    }

    ML_$LOCK(VTOC_LOCK_ID);

    /* Read the volume label block (block 0) */
    label_block = (uint32_t *)DBUF_$GET_BLOCK(vol_idx, 0, &LV_LABEL_$UID, 0, 0, 0,
                                              status_ret);

    if (*status_ret == status_$ok) {
        /* Calculate per-volume data offset */
        vol_offset = vol_idx * 100;

        /* Copy VTOC configuration from label block offset 0x4C to per-volume data
         * Source: label_block + 0x26 words = label_block + 0x4C bytes
         * Dest: vtoc_$data base - 0x54 + vol_offset
         * Count: 0x19 longs (25 * 4 = 100 bytes)
         */
        src = (uint32_t *)((uint8_t *)label_block + 0x4C);
        dst = (uint32_t *)(OS_DISK_DATA + vol_offset - 0x54);
        for (i = 0x18; i >= 0; i--) {
            *dst++ = *src++;
        }

        /* Set mount status based on hash_size and hash_type
         * mounted = (hash_type < 3) && (hash_size != 0)
         */
        {
            uint16_t hash_type = *(uint16_t *)(OS_DISK_DATA + vol_offset - 0x54);
            uint16_t hash_size = *(uint16_t *)(OS_DISK_DATA + vol_offset - 0x52);

            if (hash_type < 3 && hash_size != 0) {
                vtoc_$data.mounted[vol_idx] = (int8_t)0xFF;
            } else {
                vtoc_$data.mounted[vol_idx] = 0;
            }
        }

        /* Set format flag based on label version field (word 0)
         * If non-zero, use new format
         */
        if (*(int16_t *)label_block != 0) {
            vtoc_$data.format[vol_idx] = (int8_t)0xFF;
        } else {
            vtoc_$data.format[vol_idx] = 0;
        }

        /* Store write-protect flag (1-based per-volume array at base+0x270) */
        vtoc_$data.cach_wp_flag[vol_idx - 1] = param_4;

        /* Store param_2 */
        *(uint16_t *)(OS_DISK_DATA + vol_idx * 2 - 2) = param_2;

        /* If auditing enabled, extract volume name and UID for logging */
        if (AUDIT_$ENABLED < 0) {
            audit_rec.vol_uid.high = *(uint32_t *)((uint8_t *)label_block + 0x24);
            audit_rec.vol_uid.low = *(uint32_t *)((uint8_t *)label_block + 0x28);

            /* Copy volume name (32 bytes from offset 4), 0x00E38664 */
            for (i = 0x1F; i >= 0; i--) {
                audit_rec.vol_name[i] = ((char *)label_block)[i + 4];
            }

            /* Clear trailing bytes (Pascal indices 0x21..0x24 => C 0x20..0x23) */
            for (i = 0; i < 4; i++) {
                audit_rec.reserved_20[i] = 0;
            }
        }

        /* Release the label block with dirty flag (10 = write if modified) */
        DBUF_$SET_BUFF(label_block, 10, &local_status);

        /* If write protected, set the write-protect flag */
        if (local_status == 0x80007 /* status_$disk_write_protected */) {
            vtoc_$data.cach_wp_flag[vol_idx - 1] = (int8_t)0xFF;
        }

        /* Process any pending disk operations */
        if (vtoc_$data.dirty < 0) {
            OS_DISK_PROC(0);
        }
        vtoc_$data.dirty = 0;
    }

    ML_$UNLOCK(VTOC_LOCK_ID);

    if (*status_ret != status_$ok) {
        goto log_and_return;
    }

    /* If mount status not set (invalid config), dismount and return error */
    if (vtoc_$data.mounted[vol_idx] >= 0) {
        VTOC_$DISMOUNT(vol_idx, 0xFF, status_ret);
        /* 0x00E386DE `st -(SP)`: one byte, true. */
        BAT_$DISMOUNT(vol_idx, true, status_ret);
        *status_ret = status_$VTOC_uid_mismatch;
    }

done:
    /* Propagate BAT status if no error yet */
    if (*status_ret == status_$ok) {
        *status_ret = bat_status;
    }

log_and_return:
    /* Log audit event if enabled */
    if (AUDIT_$ENABLED < 0) {
        int16_t audit_param;         /* A6-0x48 */

        /* 0x00E38700 - 0x00E3870A: the record's trailing fields */
        audit_rec.wp_flag = (uint8_t)vtoc_$data.cach_wp_flag[vol_idx - 1];
        audit_rec.vol_idx = (uint16_t)vol_idx;
        audit_rec.param_2 = param_2;

        if (*status_ret == status_$ok) {
            audit_param = 0;
        } else {
            /* 0x00E3871A: clear Pascal elements 1..0x24 = the name and the
             * four reserved bytes, then set the UID to nil. */
            for (i = 0x23; i >= 0; i--) {
                ((char *)&audit_rec)[i] = 0;
            }
            audit_rec.vol_uid.high = UID_$NIL.high;
            audit_rec.vol_uid.low = UID_$NIL.low;
            audit_param = 1;
        }

        /*
         * 0x00E3874C pushes &AUDIT_$MOUNT_LV_EU (0x00E85648); 0x00E3873E
         * pea's the constant length word at 0x00E38762 (= 0x32).
         */
        AUDIT_$LOG_EVENT(&AUDIT_$MOUNT_LV_EU, (uint16_t *)&audit_param,
                         (uint32_t *)status_ret, (char *)&audit_rec,
                         &vtoc_$audit_mount_len);
    }
}
