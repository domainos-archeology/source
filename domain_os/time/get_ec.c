/*
 * TIME_$GET_EC - Return the EC2 handle of one of the clock eventcounts
 *
 * Registers TIME_$CLOCKH_EC (0xE2B0D4) and TIME_$FAST_CLOCK_EC (0xE2B0C8)
 * with EC2_$REGISTER_EC1 the first time either is needed, caching the
 * handles in the TIME_ data segment, then hands back the one selected by
 * *ec_id: 0 = the clock eventcount, 1 = the fast-clock eventcount, anything
 * else = status_$time_bad_timer_key.
 *
 * Parameters (frame 0x00E16718..0x00E16776):
 *   0x08 ec_id  - word by reference (0x00E16756)
 *   0x0C ec_ret - receives the EC2 handle (0x00E16768 / 0x00E16772)
 *   0x10 status - status return (A2); cleared first (0x00E1671C)
 *
 * Original address: 0x00e1670a, 130 bytes
 *
 * A5 = 0xE29198 (TIME_ data segment):
 *   (0x1620,A5) = 0xE2A7B8  time_$clock_ec_handle       (for 0xE2B0D4)
 *   (0x161C,A5) = 0xE2A7B4  time_$fast_clock_ec_handle  (for 0xE2B0C8)
 * Both are shared module cells (time/time_data.c), not function statics.
 *
 * EC2_$REGISTER_EC1 returns its handle in A0 (`move.l A0,(0x1620,A5)` at
 * 0x00E16734).  The status it leaves in *status is only looked at AFTER both
 * registrations (0x00E16752 tst.l (A2)); a failure in the first does not
 * stop the second.
 */

#include "time/time_internal.h"

void TIME_$GET_EC(uint16_t *ec_id, void **ec_ret, status_$t *status)
{
    uint16_t id;

    /* 0x00E1671C: clr.l (A2) */
    *status = status_$ok;

    /* 0x00E1671E..0x00E16734: register 0xE2B0D4 (TIME_$CLOCKH_EC) once */
    if (time_$clock_ec_handle == NULL) {
        time_$clock_ec_handle =
            EC2_$REGISTER_EC1((ec_$eventcount_t *)&TIME_$CLOCKH, status);
    }

    /* 0x00E16738..0x00E1674E: register 0xE2B0C8 (TIME_$FAST_CLOCK_EC) once */
    if (time_$fast_clock_ec_handle == NULL) {
        time_$fast_clock_ec_handle =
            EC2_$REGISTER_EC1(&TIME_$FAST_CLOCK_EC, status);
    }

    /* 0x00E16752: tst.l (A2) / bne.b 0x00e16782 */
    if (*status != status_$ok) {
        return;
    }

    /* 0x00E16756..0x00E16766: cmpi.w #1 / beq; tst.w / beq; else bad key */
    id = *ec_id;
    if (id == 1) {
        /* 0x00E16768: the fast-clock handle */
        *ec_ret = time_$fast_clock_ec_handle;
    } else if (id == 0) {
        /* 0x00E16772: the clock handle */
        *ec_ret = time_$clock_ec_handle;
    } else {
        /* 0x00E1677C: move.l #0xd0005,(A2) */
        *status = status_$time_bad_timer_key;
    }
}
