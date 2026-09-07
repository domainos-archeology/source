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
#include "audit/audit.h"

/* 0x00E388AA: the constant length word AUDIT_$LOG_EVENT is handed by
 * reference (`pea (0x22,PC)` at 0x00E38886; PC = 0x00E38888). */
static const uint16_t vtoc_$audit_dismount_len = 0x30;

void VTOC_$DISMOUNT(uint16_t vol_idx, uint8_t flags, status_$t *status_ret)
{
    int16_t i;
    int16_t vol_offset;
    uint32_t *label_block;
    uint32_t *src;
    uint32_t *dst;
    /*
     * A6-0x30: the 0x30-byte audit event record.  The original indexes it as
     * a Pascal 1-based array based at A6-0x31, so element N is C offset N-1;
     * vol_uid at 0x00E387E0 lands at A6-0x0C = record+0x24.
     */
    vtoc_$audit_dismount_rec_t audit_rec;
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
                    audit_rec.vol_uid.high =
                        *(uint32_t *)((uint8_t *)label_block + 0x24);
                    audit_rec.vol_uid.low =
                        *(uint32_t *)((uint8_t *)label_block + 0x28);

                    /* Copy volume name (32 bytes from offset 4), 0x00E387EC */
                    for (i = 0x1F; i >= 0; i--) {
                        audit_rec.vol_name[i] = ((char *)label_block)[i + 4];
                    }

                    /* 0x00E387FC clears Pascal elements 0x21..0x24 */
                    for (i = 0; i < 4; i++) {
                        audit_rec.reserved_20[i] = 0;
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

        /* 0x00E3887E / 0x00E38882: the record's trailing fields */
        audit_rec.flags = flags;
        audit_rec.vol_idx = (uint16_t)vol_idx;

        /*
         * 0x00E38894 pushes &AUDIT_$DISMOUNT_LV_EU (0x00E85640); 0x00E38886
         * pea's the constant length word at 0x00E388AA (= 0x30).
         */
        AUDIT_$LOG_EVENT(&AUDIT_$DISMOUNT_LV_EU, (uint16_t *)&audit_param,
                         (uint32_t *)status_ret, (char *)&audit_rec,
                         &vtoc_$audit_dismount_len);
    }
}
