/*
 * tty_$i_wait - Block a reader until input, a quit, or the break timeout
 *
 * Module-local helper of TTY_$K_GET (its only caller, 0x00E1C5B8).  Builds
 * the EC_$WAITN argument arrays for the quit eventcount of the current
 * address space and the line's input eventcount, optionally arms a
 * TIME_$ADVANCE timer for break modes 2 and 3, drops the TTY lock around the
 * wait and re-takes it, then reports which of the three woke it.
 *
 * 0x00E1C204..0x00E1C3CE (460 bytes; map: inside "I E1AED0 TTY"), verified
 * instruction by instruction (bead source-9lj).
 *
 * Frame (link.w A6,-0x6c): A2 = tty (0x8), D2b = wait_flag (byte in the high
 * half of 0xc), (0xe) = done_flag pointer, D3w = count (0x12), A3 = status
 * (0x14).
 *   -0x68  num_ecs (word)             -0x64  TIME_$ADVANCE / CANCEL status
 *   -0x60  timeout clock_t (6 bytes)  -0x58  scratch clock_t (6 bytes)
 *   -0x50  ecs[3]: quit, input, timer -0x40  vals[3], same order
 *   -0x30  time_queue_elem_t (0x1a)   -0x10  the timer eventcount
 *
 *   0x00E1C21C  ecs[1] = tty->input_ec; vals[1] = *input_ec + 1
 *   0x00E1C22E  ecs[0] = &FIM_$WIRED_DATA.quit_ec[PROC1_$AS_ID] (as*12);
 *               vals[0] = FIM_$WIRED_DATA.quit_value[as] (as*4) + 1
 *   0x00E1C262  D4b = 0 (timer armed); D0 = break_mode; btst.l D0,#3 - bit
 *               (break_mode mod 32) of 3, i.e. modes 0 and 1 - or
 *               (break_mode == 3 and count == 0): num_ecs = 2, no timer
 *   0x00E1C282  else: elem.flags (-0x1e) = 0; num_ecs = 3; EC_$INIT(&timer);
 *               ecs[2] = &timer; vals[2] = timer.value + 1;
 *               timeout = { word 0, M$MIU$LLW(tty->reserved_3C, 25000) }
 *               (clr.w -0x60, then the product longword at -0x5e: the
 *               timeout's low 32 bits are the product)
 *   0x00E1C2C4  break_mode == 2: scratch = timeout (move.l -0x60 / move.w
 *               -0x5c); D4 = true; TIME_$ADVANCE(&word 0 @0x00E1C3D0,
 *               &scratch, &timer, &elem, &status64)
 *   0x00E1C2EC  else: TIME_$CLOCK(&scratch); SUB48(&scratch,
 *               &tty->last_input_clock); SUB48(&timeout, &scratch); D4 =
 *               true; TIME_$ADVANCE(&word 0, &timeout, &timer, &elem, ..)
 *   0x00E1C338  wait_flag < 0 (bpl skips): count == 0 -> status 0x350008;
 *               either way no wait
 *   0x00E1C348  else TTY_$I_UNLOCK(tty); D2 = EC_$WAITN(ecs, vals, num_ecs);
 *               TTY_$I_LOCK(tty)
 *   0x00E1C372  D2 == 1 (quit): status 0x350007; FIM_$WIRED_DATA.quit_value[as] =
 *               FIM_$WIRED_DATA.quit_ec[as].value
 *   0x00E1C3A2  D2 == 3 (timer): *done_flag = 0xFF (st); D4 = 0
 *               (D2 == 2, input, changes nothing)
 *   0x00E1C3B0  D4 < 0 -> TIME_$CANCEL(vals[2] by value, &elem, &status64)
 *
 * The word cell at 0x00E1C3D0 (`gsk read 0xE1C3D0 2`: 00 00) is
 * TIME_$ADVANCE's is_absolute flag - a relative advance.
 *
 * Status codes (stcode.db.10.2): 0x350007 "quit while waiting for input",
 * 0x350008 "get conditional failed - no data available".
 *
 * Original address: 0x00E1C204
 * Size: 460 bytes
 */

#include "tty/tty_internal.h"
#include "proc1/proc1.h"
#include "fim/fim.h"
#include "ec/ec.h"
#include "time/time.h"
#include "cal/cal.h"
#include "math/math.h"

/* 0x00E1C3D0: the word 0 passed by reference as TIME_$ADVANCE's is_absolute */
static const uint16_t tty_$i_wait_relative = 0;

/* 0x00E1C2B0: move.w #0x61a8 - clock ticks per break-timeout unit */
#define TTY_WAIT_TIMEOUT_SCALE  0x61a8

void tty_$i_wait(tty_desc_t *tty, char wait_flag, char *done_flag,
                 uint16_t count, status_$t *status)
{
    ec_$eventcount_t *ecs[3];           /* (-0x50,A6): quit, input, timer */
    int32_t vals[3];                    /* (-0x40,A6) */
    time_queue_elem_t elem;             /* (-0x30,A6) */
    ec_$eventcount_t timer_ec;          /* (-0x10,A6) */
    clock_t timeout;                    /* (-0x60,A6) */
    clock_t scratch;                    /* (-0x58,A6) */
    status_$t adv_status;               /* (-0x64,A6) */
    int16_t num_ecs;                    /* (-0x68,A6) */
    boolean timer_armed;                /* D4b */
    uint16_t result;                    /* D2w */
    uint16_t break_mode;                /* D0w at 0x00E1C264 */
    uint32_t product;                   /* D0 at 0x00E1C2C0 */

    /* 0x00E1C21C..0x00E1C22A */
    ecs[1] = (ec_$eventcount_t *)ARCH_VA_TO_PTR(tty->input_ec);
    vals[1] = ecs[1]->value + 1;

    /* 0x00E1C22E..0x00E1C25E: as*12 into FIM_$QUIT_EC, as*4 into FIM_$QUIT_VALUE */
    ecs[0] = &FIM_$WIRED_DATA.quit_ec[PROC1_$AS_ID];
    vals[0] = (int32_t)FIM_$WIRED_DATA.quit_value[PROC1_$AS_ID] + 1;

    /* 0x00E1C262 */
    timer_armed = false;

    /* 0x00E1C264..0x00E1C27E: btst.l D0,D5 with D5 = 3 tests bit
     * (break_mode mod 32) of the constant 3 */
    break_mode = tty->break_mode;
    if (((3u >> (break_mode & 0x1f)) & 1u) != 0 ||
        (break_mode == 3 && count == 0)) {
        num_ecs = 2;                                        /* 0x00E1C278 */
    } else {
        /* 0x00E1C282..0x00E1C2C0 */
        elem.flags = 0;                                     /* clr.w (-0x1e,A6) */
        num_ecs = 3;
        EC_$INIT(&timer_ec);
        ecs[2] = &timer_ec;
        vals[2] = timer_ec.value + 1;
        product = M$MIU$LLW(tty->reserved_3C, TTY_WAIT_TIMEOUT_SCALE);
        timeout.high = product >> 16;                       /* clr.w (-0x60) + the
                                                               longword at -0x5e */
        timeout.low = (ushort)(product & 0xffffu);

        if (tty->break_mode == 2) {                         /* 0x00E1C2C4 */
            /* 0x00E1C2CC..0x00E1C2E6 */
            scratch.high = timeout.high;
            scratch.low = timeout.low;
            timer_armed = true;
            TIME_$ADVANCE((uint16_t *)&tty_$i_wait_relative, &scratch, &timer_ec,
                          &elem, &adv_status);              /* 0x00E1C32A..0x00E1C32E */
        } else {
            /* 0x00E1C2EC..0x00E1C316: scratch = now - last input time;
             * timeout -= scratch */
            TIME_$CLOCK(&scratch);
            SUB48(&scratch, (clock_t *)&tty->last_input_clock_high);
            SUB48(&timeout, &scratch);
            timer_armed = true;                             /* 0x00E1C318 */
            TIME_$ADVANCE((uint16_t *)&tty_$i_wait_relative, &timeout, &timer_ec,
                          &elem, &adv_status);              /* 0x00E1C32A..0x00E1C32E */
        }
    }

    /* 0x00E1C338: a negative wait_flag means "do not wait" */
    if (wait_flag < 0) {
        if (count == 0) {                                   /* 0x00E1C33C */
            *status = status_$tty_get_conditional_failed;   /* 0x00E1C340 */
        }
    } else {
        /* 0x00E1C348..0x00E1C370 */
        TTY_$I_UNLOCK(tty);
        result = EC_$WAITN(ecs, vals, num_ecs);
        TTY_$I_LOCK(tty);

        if (result == 1) {                                  /* 0x00E1C372 */
            /* 0x00E1C378..0x00E1C3A0: acknowledge the quit */
            *status = status_$tty_quit_while_waiting_for_input;
            FIM_$WIRED_DATA.quit_value[PROC1_$AS_ID] =
                (uint32_t)FIM_$WIRED_DATA.quit_ec[PROC1_$AS_ID].value;
        } else if (result == 3) {                           /* 0x00E1C3A2 */
            /* 0x00E1C3A8..0x00E1C3AE: the timer fired, nothing to cancel */
            timer_armed = false;
            *done_flag = (char)0xFF;
        }
    }

    /* 0x00E1C3B0..0x00E1C3C0: the wait value is passed by VALUE */
    if (timer_armed < 0) {
        TIME_$CANCEL(vals[2], &elem, &adv_status);
    }
}
