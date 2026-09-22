/*
 * CAL_$WRITE_TIMEZONE - Install a timezone record and write it to the label
 *
 * Validates the four name characters, copies the 12-byte record into
 * CAL_$TIMEZONE, and on a node with a disk writes utc_delta / tz_name to
 * label offsets 0xE0 / 0xE2 together with the current TIME_$CLOCKH at 0xB0
 * and 0xE6, under PROC1 lock 0xE.
 *
 * Parameters (frame 0x00E3E5EE..0x00E3E5F2):
 *   0x08 tz_in  - the record to install (A0)
 *   0x0C status - status return (A2)
 *
 * Original address: 0x00e3e5e0, 260 bytes
 *
 * A5 = 0xE7B030 (OS_CAL_WIRED): (A5) CAL_$TIMEZONE, (0x10,A5) CAL_$BOOT_VOLX.
 *
 *   00e3e5f6  moveq #3,D0 / moveq #1,D1                 ; 4 bytes at tz_in + 2..5
 *   00e3e5fc  move.b (0x1,A0,D1w),D2b
 *   00e3e600  cmpi.b #0x20 / bcs -> invalid              ; < 0x20 (unsigned)
 *   00e3e606  cmpi.b #0x7e / bls -> ok                   ; <= 0x7E
 *   00e3e60c  clr.w D3w / move.b D2b,D3b / cmpi.w #0xa0 / bhi -> ok   ; > 0xA0
 *   00e3e616  move.l #0x150002,(A2) / bra exit           ; else invalid
 *   00e3e620  addq.w #1,D1w / dbf
 *   00e3e626  three move.l (A1)+,(A3)+                   ; CAL_$TIMEZONE = *tz_in
 *   00e3e630  tst.b (0x00e24c4c).l / bmi -> status ok    ; NETWORK_$DISKLESS
 *   00e3e63a  move.w (0x10,A5),(-0xc,A6)                 ; vol_idx
 *   00e3e640  PROC1_$SET_LOCK(0xe)
 *   00e3e64e  DBUF_$GET_BLOCK(vol_idx, 0, &LV_LABEL_$UID, 0, 0, 0, &local) -> A3
 *   00e3e670  tst.l (-0x8,A6) / beq; failure: CLR_LOCK (not popped), *status = local, exit
 *   00e3e688  move.w (A5),(0xe0,A3)                      ; utc_delta
 *   00e3e68c  4 x move.b from (0x2,A5) to (0xe2,A3)      ; tz_name
 *   00e3e69c  move.l (0x00e2b0d4).l,(0xb0,A3)            ; TIME_$CLOCKH
 *   00e3e6a4  move.l (0xb0,A3),(0xe6,A3)
 *   00e3e6aa  DBUF_$SET_BUFF(label, 0xb, &local)         ; DIRTY|WRITEBACK|RELEASE
 *   00e3e6c0  PROC1_$CLR_LOCK(0xe) / addq.w #4
 *   00e3e6ce  move.l (-0x8,A6),D0 / beq -> ok; else *status = D0
 *   00e3e6d8  clr.l (A2)
 */

#include "cal/cal_internal.h"
#include "dbuf/dbuf.h"
#include "proc1/proc1.h"
#include "uid/uid.h"
#include "network/network.h"

void CAL_$WRITE_TIMEZONE(cal_$timezone_rec_t *tz_in, status_$t *status)
{
    uint16_t vol_idx;           /* A6-0xC */
    status_$t local_status;     /* A6-0x8 */
    bat_$label_t *label;        /* A3 */
    cal_$label_tz_t *ltz;
    uint8_t c;                  /* D2b */
    int16_t count;              /* D0w */
    int i;                      /* D1w - 1 */

    /* 0x00E3E5F6..0x00E3E622: dbf over the four name bytes */
    for (count = 3, i = 0; count >= 0; count--, i++) {
        c = (uint8_t)tz_in->tz_name[i];
        if (c < 0x20 || (c > 0x7E && c <= 0xA0)) {
            /* 0x00E3E616 */
            *status = status_$cal_date_or_time_invalid;
            return;
        }
    }

    /* 0x00E3E626..0x00E3E62E */
    CAL_$TIMEZONE = *tz_in;

    /* 0x00E3E630: tst.b / bmi - a Domain boolean, true is negative */
    if (NETWORK_$DISKLESS >= 0) {
        /* 0x00E3E63A */
        vol_idx = (uint16_t)CAL_$BOOT_VOLX;

        /* 0x00E3E640..0x00E3E64C */
        PROC1_$SET_LOCK(CAL_LOCK_ID);

        /* 0x00E3E64E..0x00E3E66E */
        label = (bat_$label_t *)DBUF_$GET_BLOCK(vol_idx, 0, &LV_LABEL_$UID,
                                                0, 0, 0, &local_status);

        /* 0x00E3E670 */
        if (local_status != status_$ok) {
            /* 0x00E3E676..0x00E3E686 */
            PROC1_$CLR_LOCK(CAL_LOCK_ID);
            *status = local_status;
            return;
        }

        /* 0x00E3E688..0x00E3E69A: from the GLOBAL record, not tz_in */
        ltz = CAL_$LABEL_TZ(label);
        ltz->utc_delta = CAL_$TIMEZONE.utc_delta;
        for (i = 0; i < 4; i++) {
            ltz->tz_name[i] = CAL_$TIMEZONE.tz_name[i];
        }

        /* 0x00E3E69C..0x00E3E6A8 */
        label->mount_time_high = TIME_$CLOCKH;
        ltz->last_valid_time = label->mount_time_high;

        /* 0x00E3E6AA..0x00E3E6BC */
        DBUF_$SET_BUFF(label,
                       DBUF_FLAG_DIRTY | DBUF_FLAG_WRITEBACK | DBUF_FLAG_RELEASE,
                       &local_status);

        /* 0x00E3E6C0..0x00E3E6CC */
        PROC1_$CLR_LOCK(CAL_LOCK_ID);

        /* 0x00E3E6CE..0x00E3E6D6: the SET_BUFF status is reported if non-zero */
        if (local_status != status_$ok) {
            *status = local_status;
            return;
        }
    }

    /* 0x00E3E6D8 */
    *status = status_$ok;
}
