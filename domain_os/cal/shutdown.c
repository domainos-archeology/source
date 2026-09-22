/*
 * CAL_$SHUTDOWN - Stamp the LV label with the current time
 *
 * Reads block 0 of the boot volume and writes TIME_$CLOCKH into the label's
 * mount_time_high (0xB0) and last_valid_time (0xE6) fields, then releases
 * the buffer dirty for write-back.  Unlike CAL_$READ/WRITE_TIMEZONE no PROC1
 * lock is taken.
 *
 * Parameters:
 *   status - status return ((0x8,A6), A2); handed straight to both DBUF calls
 *
 * Original address: 0x00e3e6e4, 94 bytes
 *
 * A5 = 0xE7B030; (0x10,A5) = CAL_$BOOT_VOLX.
 *
 *   00e3e6f6  subq.l #2 (result slot) / pea (A2) / clr.l / clr.l /
 *             #0xe17394 LV_LABEL_$UID / clr.l / move.w (0x10,A5) /
 *             jsr DBUF_$GET_BLOCK / lea (0x18,SP),SP -> A3
 *   00e3e716  tst.l (A2) / bne -> exit with DBUF's status
 *   00e3e71a  move.l (0x00e2b0d4).l,(0xb0,A3)      ; TIME_$CLOCKH
 *   00e3e722  move.l (0xb0,A3),(0xe6,A3)
 *   00e3e728  subq.l #2 / pea (A2) / move.w #0xb / pea (A3) /
 *             jsr DBUF_$SET_BUFF                    ; args reclaimed by unlk
 */

#include "cal/cal_internal.h"
#include "dbuf/dbuf.h"
#include "uid/uid.h"

void CAL_$SHUTDOWN(status_$t *status)
{
    bat_$label_t *label;    /* A3 */

    /* 0x00E3E6F6..0x00E3E714 */
    label = (bat_$label_t *)DBUF_$GET_BLOCK((uint16_t)CAL_$BOOT_VOLX, 0,
                                            &LV_LABEL_$UID, 0, 0, 0, status);

    /* 0x00E3E716 */
    if (*status == status_$ok) {
        /* 0x00E3E71A..0x00E3E726 */
        label->mount_time_high = TIME_$CLOCKH;
        CAL_$LABEL_TZ(label)->last_valid_time = label->mount_time_high;

        /* 0x00E3E728..0x00E3E732: DIRTY | WRITEBACK | RELEASE */
        DBUF_$SET_BUFF(label,
                       DBUF_FLAG_DIRTY | DBUF_FLAG_WRITEBACK | DBUF_FLAG_RELEASE,
                       status);
    }
}
