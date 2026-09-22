/*
 * TIME_$WAIT2 - Wait for a delay or for a caller-supplied eventcount
 *
 * Schedules a one-shot TIME_$ADVANCE on a private eventcount, then waits
 * on {*extra_ec, private ec} for {*count, 1}.  If the private eventcount
 * (index 1) is not the one that fired, the timer element is cancelled.
 * Returns a Domain boolean: TRUE (0xFF) when the caller's eventcount was
 * the one satisfied (`seq` on the index being 0), FALSE when the timer
 * expired.
 *
 * Parameters (frame 0x00E1665C..0x00E16664):
 *   0x08 delay_type - word by reference, copied to -0x38 for TIME_$ADVANCE
 *   0x0C delay      - clock_t, passed on by value of the pointer
 *   0x10 extra_ec   - eventcount to wait on alongside the timer
 *   0x14 count      - by reference: the value to wait for on *extra_ec
 *   0x18 status     - status return (A2)
 *
 * Original address: 0x00e16654, 182 bytes
 *
 * Frame: -0x38 delay_type copy, -0x34 status, -0x30 elem (0x1A bytes; its
 * flags word is -0x1E), -0x10 ec.
 *
 *   00e16668  clr.l (A2) / clr.w (-0x1e,A6)             ; elem.flags = 0
 *   00e1666e  EC_$INIT(&ec)
 *   00e1667a  TIME_$ADVANCE(&dtype, delay, &ec, &elem, &local)
 *   00e16696  tst.l local / beq; CRASH_SYSTEM(&local); *status = local; exit
 *             ; the exit path leaves D0 as CRASH_SYSTEM left it
 *   00e166ac  EC_$WAIT({extra_ec, &ec, 0}, {*count, 1, 0}) -> D2w (0-based)
 *   00e166d2  cmpi.w #1,D2w / beq -> skip cancel
 *   00e166d8  TIME_$CANCEL(1, &elem, &local)
 *   00e166ec  tst.w (-0x1e,A6) / beq; CRASH_SYSTEM(pea (-0xa4,PC))
 *             ; 0xE166F4 - 0xA4 = 0xE16650, the SAME cell TIME_$WAIT uses
 *   00e166fc  tst.w D2w / seq D0b
 */

#include "time/time_internal.h"
#include "misc/crash_system.h"

int8_t TIME_$WAIT2(uint16_t *delay_type, clock_t *delay, void *extra_ec,
                   uint32_t *count, status_$t *status)
{
    uint16_t dtype;             /* A6-0x38 */
    status_$t local_status;     /* A6-0x34 */
    time_queue_elem_t elem;     /* A6-0x30 */
    ec_$eventcount_t ec;        /* A6-0x10 */
    int16_t which;              /* D2w */

    /* 0x00E16660..0x00E1666A */
    dtype = *delay_type;
    *status = status_$ok;
    elem.flags = 0;

    /* 0x00E1666E..0x00E16678 */
    EC_$INIT(&ec);

    /* 0x00E1667A..0x00E16692 */
    TIME_$ADVANCE(&dtype, delay, &ec, &elem, &local_status);

    /* 0x00E16696..0x00E166AA */
    if (local_status != status_$ok) {
        CRASH_SYSTEM(&local_status);
        *status = local_status;
        return 0;   /* D0 is undefined on this path in the image */
    }

    /*
     * 0x00E166AC..0x00E166D0: ecs = {extra_ec, &ec, NULL},
     * vals = {*count, 1, 0}; EC_$WAIT's result is the 0-based index.
     */
    which = EC_$WAIT((ec_$wait_ecs_t){ { (ec_$eventcount_t *)extra_ec, &ec, NULL } },
                     (ec_$wait_vals_t){ { (int32_t)*count, 1, 0 } });

    /* 0x00E166D2..0x00E166E8: anything but the timer -> cancel it */
    if (which != 1) {
        TIME_$CANCEL(1, &elem, &local_status);
    }

    /* 0x00E166EC..0x00E166F6 */
    if (elem.flags != 0) {
        CRASH_SYSTEM(&time_$c_queue_elem_in_use_crash);
    }

    /* 0x00E166FC..0x00E166FE: seq on the index */
    return (which == 0) ? (int8_t)-1 : (int8_t)0;
}
