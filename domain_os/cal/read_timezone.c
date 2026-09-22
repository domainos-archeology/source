/*
 * CAL_$READ_TIMEZONE - Load the timezone record from the boot volume label
 *
 * On a node with a disk, reads block 0 of the boot volume (the LV label)
 * under PROC1 lock 0xE and refreshes CAL_$TIMEZONE.utc_delta, .tz_name and
 * CAL_$LAST_VALID_TIME from label offsets 0xE0/0xE2/0xE6 (drift is NOT on
 * disk and is left alone).  A diskless node skips the read.  Either way the
 * caller then receives a copy of CAL_$TIMEZONE and status_$ok; only a
 * failed DBUF_$GET_BLOCK returns its status instead, without the copy.
 *
 * Parameters (frame 0x00E3E52E..0x00E3E532):
 *   0x08 tz_out - receives the 12-byte record (A2)
 *   0x0C status - status return (A3)
 *
 * Original address: 0x00e3e520, 192 bytes
 *
 * A5 = 0xE7B030 (OS_CAL_WIRED): (A5) CAL_$TIMEZONE, (0xC,A5)
 * CAL_$LAST_VALID_TIME, (0x10,A5) CAL_$BOOT_VOLX.
 *
 *   00e3e536  tst.b (0x00e24c4c).l / bmi.w 0x00e3e5ca   ; NETWORK_$DISKLESS
 *   00e3e540  move.w (0x10,A5),(-0xa,A6)                ; vol_idx = CAL_$BOOT_VOLX
 *   00e3e546  subq.l #2 / move.w #0xe / jsr PROC1_$SET_LOCK / addq.w #4
 *   00e3e554  subq.l #2 (result slot) / pea status / clr.l flags+type /
 *             clr.l hint / #0xe17394 LV_LABEL_$UID / clr.l block /
 *             move.w vol_idx / jsr DBUF_$GET_BLOCK / lea (0x18,SP),SP -> A4
 *   00e3e576  tst.l (-0x8,A6) / beq: failure ->
 *   00e3e57c    PROC1_$CLR_LOCK(0xe) (args not popped); *status = local; exit
 *   00e3e58e  move.w (0xe0,A4),(A5)                     ; utc_delta
 *   00e3e592  4 x move.b (A0)+,(A1)+ from (0xe2,A4) to (0x2,A5)   ; tz_name
 *   00e3e5a2  move.l (0xe6,A4),(0xc,A5)                 ; CAL_$LAST_VALID_TIME
 *   00e3e5a8  subq.l #2 / pea status / move.w #0x8 / pea (A4) /
 *             jsr DBUF_$SET_BUFF / lea (0xc,SP),SP      ; DBUF_FLAG_RELEASE
 *   00e3e5be  PROC1_$CLR_LOCK(0xe)                      ; args not popped
 *   00e3e5ca  three move.l (A0)+,(A1)+ from A5 to A2     ; *tz_out = CAL_$TIMEZONE
 *   00e3e5d4  clr.l (A3)
 *
 * The status DBUF_$SET_BUFF writes into the local is never looked at.
 */

#include "cal/cal_internal.h"
#include "dbuf/dbuf.h"
#include "proc1/proc1.h"
#include "uid/uid.h"
#include "network/network.h"

void CAL_$READ_TIMEZONE(cal_$timezone_rec_t *tz_out, status_$t *status)
{
    uint16_t vol_idx;           /* A6-0xA */
    status_$t local_status;     /* A6-0x8 */
    bat_$label_t *label;        /* A4 */
    cal_$label_tz_t *ltz;
    int i;

    /* 0x00E3E536: tst.b / bmi - a Domain boolean, true is negative */
    if (NETWORK_$DISKLESS >= 0) {
        /* 0x00E3E540 */
        vol_idx = (uint16_t)CAL_$BOOT_VOLX;

        /* 0x00E3E546..0x00E3E552 */
        PROC1_$SET_LOCK(CAL_LOCK_ID);

        /* 0x00E3E554..0x00E3E574: block 0, LV label uid, hint 0, type 0, flags 0 */
        label = (bat_$label_t *)DBUF_$GET_BLOCK(vol_idx, 0, &LV_LABEL_$UID,
                                                0, 0, 0, &local_status);

        /* 0x00E3E576: tst.l (-0x8,A6) / beq.b */
        if (local_status != status_$ok) {
            /* 0x00E3E57C..0x00E3E58C */
            PROC1_$CLR_LOCK(CAL_LOCK_ID);
            *status = local_status;
            return;
        }

        /* 0x00E3E58E..0x00E3E5A2 */
        ltz = CAL_$LABEL_TZ(label);
        CAL_$TIMEZONE.utc_delta = ltz->utc_delta;
        for (i = 0; i < 4; i++) {
            CAL_$TIMEZONE.tz_name[i] = ltz->tz_name[i];
        }
        CAL_$LAST_VALID_TIME = ltz->last_valid_time;

        /* 0x00E3E5A8..0x00E3E5BA */
        DBUF_$SET_BUFF(label, DBUF_FLAG_RELEASE, &local_status);

        /* 0x00E3E5BE..0x00E3E5C4 */
        PROC1_$CLR_LOCK(CAL_LOCK_ID);
    }

    /* 0x00E3E5CA..0x00E3E5D4 */
    *tz_out = CAL_$TIMEZONE;
    *status = status_$ok;
}
