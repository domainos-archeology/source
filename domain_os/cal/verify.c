/*
 * CAL_$VERIFY - Check the calendar against the label's last valid time
 *
 * Reloads the timezone record from the boot volume (crashing if that
 * fails), clears the drift correction, and compares TIME_$CLOCKH with
 * CAL_$LAST_VALID_TIME.  If the clock is more than 0xE5 high-words behind,
 * or more than *max_delta ahead, a message is printed; then, when the
 * caller asked for interaction, the operator is asked whether to run with
 * the current calendar.  Returns a Domain boolean: true when the calendar
 * is acceptable (in range, or the operator said Y).
 *
 * Parameters (frame 0x00E68388, 0x00E683DE, 0x00E683EC, 0x00E683FE):
 *   0x08 max_delta   - longword by reference: largest acceptable
 *                      TIME_$CLOCKH - CAL_$LAST_VALID_TIME
 *   0x0C msg_arg     - passed by value as the second argument of the
 *                      "More than %a days" VFMT call (0x00E683EC); never
 *                      dereferenced here
 *   0x10 interactive - Domain boolean by reference (`tst.b (A0)` / bpl at
 *                      0x00E68402): true = prompt, false = just fail
 *   0x14 status_ret  - status return (A2)
 *
 * Returns (D0b): 0xFF / 0.  The C signature keeps `char` because that is
 * what the os/ caller and its host mock use; the value is a Domain boolean.
 *
 * Original address: 0x00e68380, 246 bytes
 *
 * Frame: -0x20 status, -0x18 input (TERM_$READ buffer), -0x10 tz record.
 *
 *   00e6838c  st D2b                             ; result = true
 *   00e6838e  clr.l (A2)
 *   00e68390  CAL_$READ_TIMEZONE(&tz, &status)
 *   00e683a0  clr.l (0x00e7b036).l / clr.w (0x00e7b03a).l   ; drift = 0
 *   00e683ac  tst.l status / beq; CRASH_SYSTEM(&status)
 *   00e683be  D0 = TIME_$CLOCKH - CAL_$LAST_VALID_TIME
 *   00e683ca  cmpi.l #-0xe5,D0 / bge ->
 *   00e683d2    pea 0xE684E4 / move.l (SP),-(SP) / pea 0xE68476 -> WRITE10
 *   00e683de  else: cmp.l (max_delta),D0 / ble -> return true
 *   00e683e8    pea 0xE68564 / move.l msg_arg / pea 0xE68528 -> WRITE10
 *   00e683f4  jsr VFMT_$WRITE10 / lea (0xc,SP),SP
 *   00e683fe  tst.b (interactive) / bpl -> result = false, return
 *   00e68408  loop: pea 0xE684E4 / dup / pea 0xE684A8 -> WRITE10 (prompt)
 *   00e6841c    TERM_$READ(0xE684A4, &input, 0xE684A6, &status)
 *   00e68436    'Y'/'y' -> return true; 'N'/'n' -> fall out; else loop
 *   00e68452  pea 0xE684E4 / dup / pea 0xE684E8 -> WRITE10 (SET_TIME hint)
 *   00e68462  *status_ret = 0x150007 (status_$cal_refused)
 *   00e68468  clr.b D2b
 *   00e6846a  move.b D2b,D0b
 */

#include "cal/cal_internal.h"
#include "misc/crash_system.h"
#include "term/term.h"
#include "vfmt/vfmt.h"

/*
 * `pea (d,PC)` cells in the code region 0x00E68476..0x00E68567 (between
 * CAL_$VERIFY and CAL_$GET_INFO), bytes from `gsk read 0xE68476 0xF2`.
 * The VFMT strings are terminated by their `%.` / `%$` directives; the
 * bytes after them are the image's padding, kept so the cells stay
 * byte-identical.
 */
static const char cal_$s_minute_slow[] =                    /* 0x00E68476 */
    "%/The calendar is more than a minute slow. %.";
static const int16_t cal_$c_read_line = 0x0001;             /* 0x00E684A4 */
static const int16_t cal_$c_read_limit = 0x0006;            /* 0x00E684A6 */
static const char cal_$s_prompt[] =                         /* 0x00E684A8 */
    "Do you want to run DOMAIN_OS with the current calendar? %$ H";
static const uint32_t cal_$c_no_args = 0x00000000;          /* 0x00E684E4 */
static const char cal_$s_set_calendar[] =                   /* 0x00E684E8 */
    "%/Please set the calendar using the offline CALENDAR program.%.";
static const char cal_$s_days_elapsed[] =                   /* 0x00E68528 */
    "%/More than %a days have elapsed since the last shutdown. %.";
static const uint32_t cal_$c_days_arg = 0x00000003;         /* 0x00E68564 */

/* "  The calendar ..." reference bytes: 45 + NUL, 60 + NUL, 63 + NUL, 60 + NUL */
_Static_assert(sizeof(cal_$s_minute_slow) == 0x2E, "cal_$s_minute_slow: 0x00E68476..0x00E684A3");
_Static_assert(sizeof(cal_$s_prompt) == 0x3D, "cal_$s_prompt: 0x00E684A8..0x00E684E4");
_Static_assert(sizeof(cal_$s_set_calendar) == 0x40, "cal_$s_set_calendar: 0x00E684E8..0x00E68527");
_Static_assert(sizeof(cal_$s_days_elapsed) == 0x3D, "cal_$s_days_elapsed: 0x00E68528..0x00E68564");

char CAL_$VERIFY(int *max_delta, void *msg_arg, char *interactive,
                 status_$t *status_ret)
{
    cal_$timezone_rec_t tz;     /* A6-0x10 */
    status_$t status;           /* A6-0x20 */
    char input[8];              /* A6-0x18 */
    int32_t delta;              /* D0 */
    char result;                /* D2b */

    /* 0x00E6838C..0x00E6838E */
    result = (char)0xFF;
    *status_ret = status_$ok;

    /* 0x00E68390..0x00E6839E */
    CAL_$READ_TIMEZONE(&tz, &status);

    /* 0x00E683A0..0x00E683A6 */
    CAL_$TIMEZONE.drift.high = 0;
    CAL_$TIMEZONE.drift.low = 0;

    /* 0x00E683AC..0x00E683BC */
    if (status != status_$ok) {
        CRASH_SYSTEM(&status);
    }

    /* 0x00E683BE..0x00E683C4 */
    delta = (int32_t)(TIME_$CLOCKH - CAL_$LAST_VALID_TIME);

    /* 0x00E683CA: cmpi.l #-0xe5,D0 / bge (signed) */
    if (delta < -0xE5) {
        /* 0x00E683D2..0x00E683DC, 0x00E683F4 */
        VFMT_$WRITE10(cal_$s_minute_slow, &cal_$c_no_args, &cal_$c_no_args);
    } else {
        /* 0x00E683DE..0x00E683E4: cmp.l (A0),D0 / ble (signed) */
        if (delta <= *max_delta) {
            return result;                      /* true */
        }
        /* 0x00E683E8..0x00E683F4 */
        VFMT_$WRITE10(cal_$s_days_elapsed, msg_arg, &cal_$c_days_arg);
    }

    /* 0x00E683FE..0x00E68404: tst.b (A0) / bpl -> 0x00E68468.  A Domain
     * boolean: 0xFF is true, so the test is on the SIGNED byte (char is
     * unsigned on some hosts). */
    if ((int8_t)*interactive >= 0) {
        result = 0;                             /* 0x00E68468 */
        return result;
    }

    /* 0x00E68408..0x00E68450 */
    do {
        VFMT_$WRITE10(cal_$s_prompt, &cal_$c_no_args, &cal_$c_no_args);
        (void)TERM_$READ((short *)&cal_$c_read_line, input,
                         (void *)&cal_$c_read_limit, &status);
        if (input[0] == 'Y' || input[0] == 'y') {
            return result;                      /* 0x00E6846A, still true */
        }
    } while (input[0] != 'N' && input[0] != 'n');

    /* 0x00E68452..0x00E68468 */
    VFMT_$WRITE10(cal_$s_set_calendar, &cal_$c_no_args, &cal_$c_no_args);
    *status_ret = status_$cal_refused;
    result = 0;
    return result;
}
