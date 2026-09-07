/*
 * WIN_$FORMAT_TRACK - Winchester format-track operation
 *
 * WIN_$DO_IO's op-type 3 arm (0x00E197A2 `cmpi.w #0x3,D0w`) calls this
 * routine under the module lock and returns; it is the only caller
 * (0x00E197B8).
 *
 * The routine seeks to the request's cylinder, issues controller command 9
 * with GO = 3, waits for the drive's eventcount or a 0x28-tick clock
 * timeout, and checks the resulting drive status - up to five times
 * (`moveq #0x4,D2` + `dbf`, 0x00E196C8 / 0x00E19744).  On persistent failure
 * it invalidates the request's volume and records the status in the request.
 *
 * This is the Winchester twin of FLP_FORMAT_TRACK (0x00E3DD5C); the two share
 * the volume-invalidation idiom byte for byte.
 *
 * Original address: 0x00E196AA
 * Size: 204 bytes
 */

#include "win/win_internal.h"

void WIN_$FORMAT_TRACK(void *dev_entry, win_$request_t *req)
{
    volatile uint8_t *regs;         /* A2 */
    uint16_t          unit;         /* A6-0x0E */
    const uint16_t   *cylinder_ptr; /* D5 */
    ec_$eventcount_t *win_ec;
    int32_t           wait_val;     /* A6-0x08 */
    status_$t         status;       /* D0 */
    int16_t           attempts;     /* D2 */

    /*
     * 0x00E196BA `clr.w (-0xe,A6)`: the format path only ever addresses
     * unit 0, and 0x00E196BE `clr.l (0x60,A5)` drops any request the driver
     * thought it was working on.
     */
    unit = 0;
    WIN_CUR_REQ = NULL;

    /* 0x00E196C2 `lea (A5),A0` / 0x00E196C4 `movea.l (0x4,A0),A2`. */
    regs = WIN_UNIT_REGS(0);

    /* 0x00E196DE `moveq #0x1c,D5` / 0x00E196E0 `add.l A1,D5`. */
    cylinder_ptr = (const uint16_t *)((const uint8_t *)dev_entry + 0x1C);

    win_ec = WIN_UNIT_EC(0);

    /*
     * 0x00E196C8 `moveq #0x4,D2` + 0x00E19744 `dbf D2w`: five attempts.
     * D0 survives the loop and is the status the failure path reports.
     */
    status = status_$ok;
    for (attempts = 4; ; attempts--) {
        /*
         * 0x00E196E2-0x00E196F0.  SEEK's fourth argument is `st -(SP)`,
         * i.e. the Domain boolean TRUE - WIN_$DO_IO's own seek passes
         * `clr.w -(SP)` there instead (0x00E19826).
         */
        status = SEEK(unit, *cylinder_ptr, req, 0xFF);
        if (status == status_$ok) {
            /* 0x00E196FC: clear the status word before starting. */
            regs[WIN_REG_STATUS] = 0;

            /* 0x00E19702-0x00E19708. */
            wait_val = (int32_t)win_ec->value + 1;

            /* 0x00E1970C / 0x00E19712: command 9, then GO = 3. */
            regs[WIN_REG_MODE] = WIN_MODE_FORMAT;
            regs[WIN_REG_GO] = WIN_GO_FORMAT;

            /*
             * 0x00E19718-0x00E1972A.  Two three-element arrays by value; the
             * pushes run vals[2], vals[1], vals[0], ecs[2], ecs[1], ecs[0],
             * so ecs lands at the lower address.  A4 is 0 (0x00E196D6) and
             * A3/D6 are &TIME_$CLOCKH (0x00E196CA / 0x00E196D0).  The result
             * in D0 is discarded: which eventcount fired does not matter,
             * the drive status decides.
             */
            (void)EC_$WAIT(
                (ec_$wait_ecs_t){{ win_ec,
                                   (ec_$eventcount_t *)&TIME_$CLOCKH,
                                   NULL }},
                (ec_$wait_vals_t){{ wait_val,
                                    (int32_t)(TIME_$CLOCKH + 0x28),
                                    0 }});

            /* 0x00E19734-0x00E19742. */
            status = WIN_$CHECK_DISK_STATUS(unit);
            if (status == status_$ok) {
                return;
            }
        }

        /* 0x00E19744 `dbf D2w,0x00E196E2`. */
        if (attempts == 0) {
            break;
        }
    }

    /* 0x00E19748 `tst.l D0` / `beq` - the dbf falls out with D0 still set. */
    if (status == status_$ok) {
        return;
    }

    /*
     * 0x00E1974C-0x00E19768.  The request's volume number is 1-based against
     * the disk subsystem's per-volume table, and the byte cleared is that
     * entry's +0x18.  The identical five instructions appear in WIN_$DO_IO
     * (0x00E1994A) and FLP_FORMAT_TRACK (0x00E3DDB0).
     */
    WIN_VOLUME_MOUNTED(req->volume) = 0;
    req->status = status;
}
