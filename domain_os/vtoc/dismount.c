/*
 * VTOC_$DISMOUNT - Dismount a volume's VTOC
 *
 * Original address: 0x00E38764 (SAU2 map: VTOC_, VTOC_$DISMOUNT at E38764)
 * Size: 326 bytes (0x00E38764 .. 0x00E388A9); the audit length cell
 * follows at 0x00E388AA.
 *
 * Under ML lock 0x10: if the volume is mounted and the dismount is not
 * forced, the 100-byte per-volume VTOC record is copied back into the
 * volume label block (offset 0x4C) and, when auditing is on, the label's
 * name and UID are captured for the audit record; the mount flag is then
 * cleared, OS_DISK_PROC and BAT_$DISMOUNT run, and on success the DBUF
 * volume UID is reset to UID_$NIL.  After the unlock DISK_$DISMOUNT runs
 * and the audit event is logged.
 *
 * Frame (link.w A6,-0x40; D2/D3/A2-A5 saved; A5 = 0xE784D0 = &vtoc_$data):
 *   (0x8,A6)   vol_idx     word -> D2w
 *   (0xa,A6)   flags       byte (high half of its word slot) -> D3b;
 *                          negative = forced dismount, skip the writeback
 *   (0xc,A6)   status_ret  pointer -> A2
 *   (-0x30,A6) audit_rec   the 0x30-byte audit record (vtoc_$audit_dismount_rec_t)
 *   (-0x3c,A6) audit_param word: 1 when the status is not ok
 *
 * Verified against the disassembly 2026-09-19; the body was already faithful.
 */

#include "vtoc/vtoc_internal.h"
#include "audit/audit.h"

/* 0x00E388AA: the constant length word AUDIT_$LOG_EVENT is handed by
 * reference (`pea (0x22,PC)` at 0x00E38886; PC = 0x00E38888).  Image bytes
 * 00 30. */
static const uint16_t vtoc_$audit_dismount_len = 0x30;

void VTOC_$DISMOUNT(uint16_t vol_idx, uint8_t flags, status_$t *status_ret)
{
    int16_t i;
    int16_t vol_offset;
    uint32_t *label_block;          /* A4 */
    uint32_t *src;
    uint32_t *dst;
    /*
     * A6-0x30: the 0x30-byte audit event record.  The original indexes it as
     * a Pascal 1-based array based at A6-0x31, so element N is C offset N-1;
     * vol_uid at 0x00E387E0 lands at A6-0x0C = record+0x24.
     */
    vtoc_$audit_dismount_rec_t audit_rec;
    int16_t audit_param;            /* (-0x3c,A6) */

    /* 0x00E3877E clr.l (A2) */
    *status_ret = status_$ok;

    /* 0x00E38780 .. 0x00E3878C: ML_$LOCK(0x10) */
    ML_$LOCK(VTOC_LOCK_ID);

    /* 0x00E3878E .. 0x00E38796: `tst.b (0x277,A3)` / `bpl.w 0x00e3884c` */
    if (vtoc_$data.mounted[vol_idx] < 0) {

        /* 0x00E3879A `tst.b D3b` / `bmi.b 0x00e3881a`: forced dismounts skip
         * the writeback */
        if ((int8_t)flags >= 0) {
            /* 0x00E3879E .. 0x00E387BA: DBUF_$GET_BLOCK(vol_idx, 0,
             * &LV_LABEL_$UID (0xE17394), 0, 0, 0, status_ret) */
            label_block = (uint32_t *)DBUF_$GET_BLOCK(vol_idx, 0, &LV_LABEL_$UID,
                                                      0, 0, 0, status_ret);

            /* 0x00E387BC tst.l (A2) / bne.b 0x00e3881a */
            if (*status_ret == status_$ok) {
                /* 0x00E387C0 .. 0x00E387D0: 25 longwords (`moveq #0x18` /
                 * dbf) from A5 + vol_idx*100 - 0x54 to label + 0x4C */
                vol_offset = vol_idx * 100;
                src = (uint32_t *)(OS_DISK_DATA + vol_offset - 0x54);
                dst = (uint32_t *)((uint8_t *)label_block + 0x4C);
                for (i = 0x18; i >= 0; i--) {
                    *dst++ = *src++;
                }

                /* 0x00E387D4 `tst.b (0x00e2e09e).l` / `bpl.b` */
                if (AUDIT_$ENABLED < 0) {
                    /* 0x00E387DC .. 0x00E387E4: label + 0x24 / + 0x28 */
                    audit_rec.vol_uid.high =
                        *(uint32_t *)((uint8_t *)label_block + 0x24);
                    audit_rec.vol_uid.low =
                        *(uint32_t *)((uint8_t *)label_block + 0x28);

                    /* 0x00E387E8 .. 0x00E387F4: D1 = 1..0x20,
                     * (0x3,A4,D1w) -> (-0x31,A6,D1w): label bytes 4..0x23 */
                    for (i = 0x1F; i >= 0; i--) {
                        audit_rec.vol_name[i] = ((char *)label_block)[i + 4];
                    }

                    /* 0x00E387F8 .. 0x00E38802: clears Pascal elements
                     * 0x21..0x24 */
                    for (i = 0; i < 4; i++) {
                        audit_rec.reserved_20[i] = 0;
                    }
                }

                /* 0x00E38806 .. 0x00E38816: DBUF_$SET_BUFF(label, 9, status_ret) */
                DBUF_$SET_BUFF(label_block, BAT_BUF_DIRTY, status_ret);
            }
        }

        /* 0x00E3881A clr.b (0x277,A3) */
        vtoc_$data.mounted[vol_idx] = 0;

        /* 0x00E3881E .. 0x00E38826: OS_DISK_PROC(vol_idx), result slot unused */
        OS_DISK_PROC(vol_idx);

        /*
         * 0x00E38828 .. 0x00E38834: BAT_$DISMOUNT(vol_idx, flags, status_ret);
         * `move.b D3b,-(SP)` forwards this routine's own byte argument
         * unchanged as one byte.
         */
        BAT_$DISMOUNT(vol_idx, (boolean)flags, status_ret);

        /* 0x00E38836 tst.l (A2) / bne.b 0x00e3884c */
        if (*status_ret == status_$ok) {
            /* 0x00E3883A .. 0x00E3884A: DBUF_$UPDATE_VOL(vol_idx, &UID_$NIL
             * (0xE1737C)), result slot unused */
            DBUF_$UPDATE_VOL(vol_idx, &UID_$NIL);
        }
    }

    /* 0x00E3884C .. 0x00E38858: ML_$UNLOCK(0x10) */
    ML_$UNLOCK(VTOC_LOCK_ID);

    /* 0x00E3885A .. 0x00E38864: DISK_$DISMOUNT(vol_idx), result slot unused */
    DISK_$DISMOUNT(vol_idx);

    /* 0x00E38866 `tst.b (0x00e2e09e).l` / `bpl.b 0x00e388a0` */
    if (AUDIT_$ENABLED < 0) {
        /* 0x00E3886E .. 0x00E38878 */
        audit_param = (*status_ret != status_$ok) ? 1 : 0;

        /* 0x00E3887E / 0x00E38882: the record's trailing fields */
        audit_rec.flags = flags;
        audit_rec.vol_idx = (uint16_t)vol_idx;

        /*
         * 0x00E38886 .. 0x00E3889A: AUDIT_$LOG_EVENT(&AUDIT_$DISMOUNT_LV_EU
         * (0x00E85640), &audit_param, status_ret, &audit_rec, &len) where
         * len is the constant word at 0x00E388AA (= 0x30), `pea (0x22,PC)`.
         */
        AUDIT_$LOG_EVENT(&AUDIT_$DISMOUNT_LV_EU, (uint16_t *)&audit_param,
                         (uint32_t *)status_ret, (char *)&audit_rec,
                         &vtoc_$audit_dismount_len);
    }
}
