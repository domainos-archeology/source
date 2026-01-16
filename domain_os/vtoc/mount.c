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

/* External function declarations */
extern void DISK_$WRITE_PROTECT(int16_t flag, int16_t vol_idx, status_$t *status);
extern void OS_DISK_PROC(int16_t vol_idx);
extern void AUDIT_$LOG_EVENT(uint32_t event_id, int16_t *param1, int16_t *param2,
                             char *data, uint16_t flags);

/* External variables */
extern int8_t AUDIT_$ENABLED;    /* 0xE2E09E: Audit enabled flag */
extern int8_t DAT_00e78756;      /* VTOC dirty flag */
extern uint32_t VTOC_CACH_LOOKUPS;  /* Cache lookup count / flags */

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
    uid_t vol_uid;
    char name_buf[36];

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
    label_block = (uint32_t *)DBUF_$GET_BLOCK(vol_idx, 0, &LV_LABEL_$UID, 0, 0, status_ret);

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

        /* Store flags */
        ((char *)&VTOC_CACH_LOOKUPS)[vol_idx + 3] = param_4;

        /* Store param_2 */
        *(uint16_t *)(OS_DISK_DATA + vol_idx * 2 - 2) = param_2;

        /* If auditing enabled, extract volume name and UID for logging */
        if (AUDIT_$ENABLED < 0) {
            vol_uid.high = *(uint32_t *)((uint8_t *)label_block + 0x24);
            vol_uid.low = *(uint32_t *)((uint8_t *)label_block + 0x28);

            /* Copy volume name (32 bytes from offset 4) */
            for (i = 0x1F; i >= 0; i--) {
                name_buf[i] = ((char *)label_block)[i + 4];
            }

            /* Clear trailing bytes */
            for (i = 0; i < 4; i++) {
                name_buf[0x21 + i] = 0;
            }
        }

        /* Release the label block with dirty flag (10 = write if modified) */
        DBUF_$SET_BUFF(label_block, 10, &local_status);

        /* If write protected, set cache flag */
        if (local_status == 0x80007 /* status_$disk_write_protected */) {
            ((uint8_t *)&VTOC_CACH_LOOKUPS)[vol_idx + 3] = 0xFF;
        }

        /* Process any pending disk operations */
        if (DAT_00e78756 < 0) {
            OS_DISK_PROC(0);
        }
        DAT_00e78756 = 0;
    }

    ML_$UNLOCK(VTOC_LOCK_ID);

    if (*status_ret != status_$ok) {
        goto log_and_return;
    }

    /* If mount status not set (invalid config), dismount and return error */
    if (vtoc_$data.mounted[vol_idx] >= 0) {
        VTOC_$DISMOUNT(vol_idx, 0xFF, status_ret);
        BAT_$DISMOUNT(vol_idx, 0xFFC4, status_ret);
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
        int16_t audit_param;

        if (*status_ret == status_$ok) {
            audit_param = 0;
        } else {
            /* Clear name buffer on error */
            for (i = 0x23; i >= 0; i--) {
                name_buf[i] = 0;
            }
            vol_uid.high = UID_$NIL.high;
            vol_uid.low = UID_$NIL.low;
            audit_param = 1;
        }

        AUDIT_$LOG_EVENT(0x5648, &audit_param, (int16_t *)status_ret,
                        name_buf, 0x8762);
    }
}
