/*
 * VTOC_$DISMOUNT - Dismount a volume's VTOC
 *
 * Original address: 0x00e38764
 * Size: 326 bytes
 *
 * Flushes cached VTOC data back to the volume label and marks
 * the volume as dismounted.
 */

#include "vtoc/vtoc_internal.h"

/* External function declarations */
extern void DISK_$DISMOUNT(int16_t vol_idx);
extern void OS_DISK_PROC(int16_t vol_idx);
extern void DBUF_$UPDATE_VOL(int16_t vol_idx, uid_t *uid);
extern void AUDIT_$LOG_EVENT(uint32_t event_id, int16_t *param1, int16_t *param2,
                             char *data, uint16_t flags);

/* External variables */
extern int8_t AUDIT_$ENABLED;    /* 0xE2E09E: Audit enabled flag */

void VTOC_$DISMOUNT(uint16_t vol_idx, uint8_t flags, status_$t *status_ret)
{
    int16_t i;
    int16_t vol_offset;
    uint32_t *label_block;
    uint32_t *src;
    uint32_t *dst;
    uid_t vol_uid;
    char name_buf[36];
    int16_t audit_param;

    *status_ret = status_$ok;

    ML_$LOCK(VTOC_LOCK_ID);

    /* Only proceed if volume is mounted */
    if (vtoc_$data.mounted[vol_idx] < 0) {

        /* If not forced dismount (flags >= 0), write back VTOC data to label */
        if ((int8_t)flags >= 0) {
            /* Read the volume label block */
            label_block = (uint32_t *)DBUF_$GET_BLOCK(vol_idx, 0, &LV_LABEL_$UID,
                                                      0, 0, status_ret);

            if (*status_ret == status_$ok) {
                /* Calculate per-volume data offset */
                vol_offset = vol_idx * 100;

                /* Copy VTOC configuration back to label block
                 * Source: vtoc_$data base - 0x54 + vol_offset
                 * Dest: label_block + 0x4C bytes
                 * Count: 0x19 longs (25 * 4 = 100 bytes)
                 */
                src = (uint32_t *)(OS_DISK_DATA + vol_offset - 0x54);
                dst = (uint32_t *)((uint8_t *)label_block + 0x4C);
                for (i = 0x18; i >= 0; i--) {
                    *dst++ = *src++;
                }

                /* If auditing enabled, extract volume name and UID for logging */
                if (AUDIT_$ENABLED < 0) {
                    vol_uid.high = *(uint32_t *)((uint8_t *)label_block + 0x24);
                    vol_uid.low = *(uint32_t *)((uint8_t *)label_block + 0x28);

                    /* Copy volume name (32 bytes from offset 4) */
                    for (i = 0x1F; i >= 0; i--) {
                        name_buf[i + 1] = ((char *)label_block)[i + 4];
                    }

                    /* Clear trailing bytes */
                    for (i = 0; i < 4; i++) {
                        name_buf[0x21 + i] = 0;
                    }
                }

                /* Release the label block as dirty */
                DBUF_$SET_BUFF(label_block, BAT_BUF_DIRTY, status_ret);
            }
        }

        /* Clear mount status */
        vtoc_$data.mounted[vol_idx] = 0;

        /* Process disk operations for this volume */
        OS_DISK_PROC(vol_idx);

        /* Dismount the BAT */
        BAT_$DISMOUNT(vol_idx, (uint16_t)((uint8_t)flags << 8) | 0x26, status_ret);

        /* Update volume UID to nil if successful */
        if (*status_ret == status_$ok) {
            DBUF_$UPDATE_VOL(vol_idx, &UID_$NIL);
        }
    }

    ML_$UNLOCK(VTOC_LOCK_ID);

    /* Call disk dismount */
    DISK_$DISMOUNT(vol_idx);

    /* Log audit event if enabled */
    if (AUDIT_$ENABLED < 0) {
        audit_param = (*status_ret != status_$ok) ? 1 : 0;

        /* Audit log requires name_buf at offset 1 */
        AUDIT_$LOG_EVENT(0x5640, &audit_param, (int16_t *)status_ret,
                        &name_buf[1], 0x88AA);
    }
}
