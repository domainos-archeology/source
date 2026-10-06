/*
 * win/disk_init.c - DISK_INIT
 *
 * Original address: 0x00E19986, 566 bytes.
 *
 * Despite the name this is a WIN-internal routine, not a DISK entry point:
 * it lives inside the Winchester driver's code region and its only callers
 * are WIN_$DINIT (0x00E19D32) and FUN_00e194b4 (0x00E194E0), both by `bsr`
 * with the status returned in D0.  Bead source-1nob.
 *
 * It brings one drive up: clear fault, ask for general status, spin the
 * platter up if it is not already spinning, enable writes, then read the
 * drive's attribute number and, if the caller has no geometry yet, fill in
 * the geometry of the drive it recognises.
 *
 * The whole body up to the attribute read is a ten-attempt retry loop
 * (`moveq #0x9,D5` + `dbf` at 0x00E199C8 / 0x00E19AA2).
 *
 * Every basic block of the original is accounted for; the addresses in the
 * comments say which instructions each statement stands for.
 */

#include "win/win_internal.h"

#include "ec/ec.h"
#include "math/math.h"
/*
 * ============================================================================
 * Constant cells in the WIN code region, passed by `pea (d,PC)`
 * ============================================================================
 *
 * Domain Pascal passes `var`/`const` parameters by address, so a literal
 * argument becomes a cell in the code region whose address is pushed.
 */

/*
 * 0x00E1940A: a zero word.  Handed to WIN_$ANSI_COMMAND as ansi_in_param for
 * REPORT GENERAL STATUS (0x00E199F8) and REPORT DRIVE ATTRIBUTE (0x00E19AD8).
 * Both codes are below ANSI_CMD_TAKES_INPUT, so the callee never reads it.
 */
static const char win_$ansi_no_input[2] = {0, 0};

/*
 * 0x00E19BBC: the byte 0x80.  ansi_in_param for SPIN CONTROL (0x00E19A3C) and
 * WRITE CONTROL (0x00E19A82); both are >= ANSI_CMD_TAKES_INPUT, so the callee
 * stores this byte into the drive's parameter register.  Bit 7 is "on".
 */
static const char win_$ansi_bit7_set[2] = {(char)0x80, 0};

/*
 * 0x00E19BBE: the byte 0x02.  ansi_in_param for LOAD ATTRIBUTE NUMBER
 * (0x00E19AB0) -- attribute 2 is the drive identifier.
 */
static const char win_$ansi_attribute_2[2] = {0x02, 0};

status_$t DISK_INIT(uint16_t unit, uint16_t sub_unit, int32_t *total_blocks,
                    uint16_t *blocks_per_track, uint16_t *heads,
                    uint16_t *geometry, uint16_t *drive_id)
{
    ec_$eventcount_t *unit_ec;
    status_$t status;     /* D3, the status the retries carry */
    int16_t attempt;      /* D5 */
    char general_status;  /* (-0x12,A6) */
    char attribute;       /* (-0x16,A6) */
    uint8_t drive;        /* D4 then D1: the attribute's low nibble */
    uint16_t cylinders;   /* D1 */
    int32_t wait_value;   /* (-0x8,A6) */
    int32_t track_count;

    /*
     * 0x00E199A4-0x00E199AE: only sub-unit 0 exists on a Winchester.  The
     * refusal branches straight to the register restore, so the returned
     * value is this constant and not the D3 the rest of the body builds.
     */
    if (sub_unit != 0) {
        return status_$invalid_unit_number;
    }

    WIN_CUR_REQ_VA = 0;          /* 0x00E199B2: clr.l (0x60,A5) */
    unit_ec = WIN_UNIT_EC(unit); /* 0x00E19A2E / 0x00E19A62 */

    /*
     * 0x00E199C8: `moveq #0x9,D5` with a `dbf` at the bottom, so ten
     * attempts.  D3 is only ever tested after an attempt has failed, so on
     * the fall-out at 0x00E19AA6 it is always non-zero.
     */
    status = status_$ok;
    for (attempt = 9;; attempt--) {
        /*
         * 0x00E199D6-0x00E199F2: park the parameter register at 1 and start
         * a type-6 command, then wait for the controller.  The result of the
         * wait is discarded (the caller only reserved the argument slot).
         */
        /* the regs pointer is re-read for each store, as the original does */
        *(WIN_UNIT_REGS(unit) + WIN_REG_PARAM) = 1; /* 0x00E199DA */
        *(WIN_UNIT_REGS(unit) + WIN_REG_GO) = 6;    /* 0x00E199E4 */
        (void)WAIT_FOR_CONTROLLER(unit); /* 0x00E199EE */

        /* 0x00E199F4-0x00E19A0A */
        status = WIN_$ANSI_COMMAND(unit, ANSI_CMD_REPORT_GENERAL_STATUS,
                                   (char *)win_$ansi_no_input, &general_status);

        /* 0x00E19A0C-0x00E19A14: the result of this one is thrown away */
        (void)WIN_$CHECK_DISK_STATUS(unit);

        if (status != status_$ok) { /* 0x00E19A16 */
            goto next_attempt;
        }

        /*
         * 0x00E19A1C-0x00E19A22: bit 0 of the general status means the
         * spindle is stopped, so spin it up and wait for the drive's
         * eventcount (or for the clock, whichever comes first).
         */
        if ((general_status & 0x01) != 0) {
            *(WIN_UNIT_REGS(unit) + WIN_REG_MODE) = 0x0A; /* 0x00E19A28 */

            /* 0x00E19A2E-0x00E19A34: the value to wait the eventcount to */
            wait_value = unit_ec->value + 1;

            /* 0x00E19A38-0x00E19A4A */
            (void)WIN_$ANSI_COMMAND(unit, ANSI_CMD_SPIN_CONTROL,
                                    (char *)win_$ansi_bit7_set,
                                    &general_status);

            /*
             * 0x00E19A4E-0x00E19A6C.  EC_$WAIT takes both three-element
             * arrays BY VALUE (24 bytes); the pushes are, in order,
             * vals[2], vals[1], vals[0], ecs[2], ecs[1], ecs[0].  The second
             * eventcount is TIME_$CLOCKH itself, so the wait doubles as a
             * 0x78-tick (about 31 s) timeout.
             */
            (void)EC_$WAIT(
                (ec_$wait_ecs_t){{unit_ec,
                                  (ec_$eventcount_t *)&TIME_$CLOCKH, NULL}},
                (ec_$wait_vals_t){{wait_value,
                                   (int32_t)(WIN_CLOCKH() + 0x78), 0}});

            /* 0x00E19A70-0x00E19A7C */
            status = WIN_$CHECK_DISK_STATUS(unit);
            if (status != status_$ok) {
                goto next_attempt;
            }
        }

        /* 0x00E19A7E-0x00E19A90: turn writing on */
        (void)WIN_$ANSI_COMMAND(unit, ANSI_CMD_WRITE_CONTROL,
                                (char *)win_$ansi_bit7_set, &general_status);

        /* 0x00E19A94-0x00E19AA0 */
        status = WIN_$CHECK_DISK_STATUS(unit);
        if (status == status_$ok) {
            break;
        }

    next_attempt:
        /* 0x00E19AA2: dbf D5w */
        if (attempt == 0) {
            /*
             * 0x00E19AA6-0x00E19AA8.  Every path that reaches the `dbf`
             * has a non-zero status, so this always returns; the test is
             * kept because that is what the code does.
             */
            if (status != status_$ok) {
                return status;
            }
            break;
        }
    }

    /*
     * 0x00E19AAC-0x00E19AC2: select attribute 2 ...
     */
    (void)WIN_$ANSI_COMMAND(unit, ANSI_CMD_LOAD_ATTRIBUTE_NUMBER,
                            (char *)win_$ansi_attribute_2, &attribute);
    drive = (uint8_t)attribute;

    /* 0x00E19AC6-0x00E19AD2 */
    if (WIN_$CHECK_DISK_STATUS(unit) == status_$ok) {
        /* 0x00E19AD4-0x00E19AEA: ... and read it back */
        (void)WIN_$ANSI_COMMAND(unit, ANSI_CMD_REPORT_DRIVE_ATTRIBUTE,
                                (char *)win_$ansi_no_input, &attribute);
        drive = (uint8_t)attribute;
    }

    /* 0x00E19AEE-0x00E19AFA */
    status = WIN_$CHECK_DISK_STATUS(unit);

    /*
     * 0x00E19AF8-0x00E19B0C: D1 holds the drive identifier for the rest of
     * the routine.  The reported id is 0x0100 + the low nibble.
     */
    cylinders = (uint16_t)(drive & 0x0F);
    *drive_id = (uint16_t)(WIN_DRIVE_ID_BASE + cylinders);

    if (status != status_$ok) { /* 0x00E19B0E */
        return status;
    }

    /*
     * 0x00E19B14-0x00E19B18: a caller that already knows how big the volume
     * is keeps its own geometry.  `tst.l / bgt` is a signed test, so zero and
     * negative both fall through.
     */
    if (*total_blocks > 0) {
        return status;
    }

    geometry[0] = 0; /* 0x00E19B20: clr.w (A1) */

    /*
     * 0x00E19B22-0x00E19B84.  Note that D1 is reused: on the three
     * recognised drives it is overwritten with the cylinder count, but on
     * the unrecognised path it still holds the drive identifier and the
     * multiply below runs with it anyway.
     */
    switch (cylinders) {
    case WIN_DRIVE_PRIAM_3450: /* 0x00E19B38 */
        *blocks_per_track = 12;
        *heads = 5;
        cylinders = 525; /* 0x20D */
        geometry[1] = 1120; /* 0x00E19B78: 0x460 */
        break;

    case WIN_DRIVE_MICROPOLIS_1203: /* 0x00E19B4C */
        *blocks_per_track = 12;
        *heads = 5;
        cylinders = 525;
        geometry[1] = 1181; /* 0x00E19B5E: 0x49D */
        break;

    case WIN_DRIVE_PRIAM_7050: /* 0x00E19B66 */
        *blocks_per_track = 12;
        *heads = 5;
        cylinders = 1049;   /* 0x419 */
        geometry[1] = 1120; /* 0x00E19B78, reached by fall-through */
        break;

    default: /* 0x00E19B80 */
        status = status_$unrecognized_drive_id;
        /*
         * The original falls straight into the multiply at 0x00E19B86 with
         * D1 still holding the drive id and *heads / *blocks_per_track
         * whatever the caller left there, and stores the product over
         * *total_blocks before returning the error.  Reproduced as written.
         */
        break;
    }

    /*
     * 0x00E19B86-0x00E19BAE.  M$MIS$LLW is a signed 32x16 multiply and
     * M$MIS$LLL a signed 32x32 one, both truncating to 32 bits.
     */
    track_count = M$MIS$LLW((int32_t)*heads, (int16_t)cylinders);
    *total_blocks = M$MIS$LLL(track_count, (int32_t)*blocks_per_track);

    return status; /* 0x00E19BB0: move.l D3,D0 */
}
