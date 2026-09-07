/*
 * TIME_$WAIT - Block the calling process for a specified time
 *
 * Schedules a one-shot timer callback that advances a local eventcount, then
 * waits on that eventcount together with the address space's quit eventcount
 * so that the sleep can be broken by a quit fault.
 *
 * Parameters (Pascal, all by reference):
 *   (0x08,A6) delay_type - 0 = relative to TIME_$CLOCK,
 *                          1 = relative to TIME_$ABS_CLOCK
 *   (0x0C,A6) delay      - clock_t delay value
 *   (0x10,A6) status     - status return
 *
 * Frame (link.w A6,-0x54):
 *   -0x10  ec            local eventcount (12 bytes)
 *   -0x1e  elem.flags    the "callback in use" word inside the queue element
 *   -0x30  elem          time_queue_elem_t handed to TIME_$ADVANCE
 *   -0x38  abs_clock
 *   -0x40  current_clock
 *   -0x48  local_delay
 *   -0x4c  quit_value    FIM_$QUIT_VALUE[as] + 1
 *   -0x50  local_status
 *   -0x54  dtype         the by-reference copy of *delay_type
 *
 * Original address: 0x00e1650a
 * Size: 324 bytes
 */

#include "time/time_internal.h"
#include "misc/crash_system.h"

/* 0x00E1660A: move.l #0xd0003,(A2) */

/*
 * Constant status cell passed by reference at 0x00E1663A:
 *   pea (0x14,PC)  ->  0x00E1663C + 0x14 = 0x00E16650
 * The cell at 0x00E16650 holds 0x000D000D.
 */
static const status_$t wait_crash_status_00e16650 = 0x000D000D;

void TIME_$WAIT(uint16_t *delay_type, clock_t *delay, status_$t *status)
{
    uint16_t dtype;                /* (-0x54,A6) */
    clock_t local_delay;           /* (-0x48,A6) */
    clock_t current_clock;         /* (-0x40,A6) */
    clock_t abs_clock;             /* (-0x38,A6) */
    time_queue_elem_t elem;        /* (-0x30,A6) */
    ec_$eventcount_t ec;           /* (-0x10,A6) */
    status_$t local_status;        /* (-0x50,A6) */
    int32_t quit_value;            /* (-0x4c,A6) */
    uint16_t as_id;
    int16_t which;

    /* 0xE16516 */
    *status = status_$ok;
    dtype = *delay_type;

    /* 0xE1651E: clr.w (-0x1e,A6) - the flags word inside the queue element */
    elem.flags = 0;

    EC_$INIT(&ec);

    /* 0xE16532: copy the 48-bit delay into the frame */
    local_delay.high = delay->high;
    local_delay.low = delay->low;

    /*
     * 0xE1653C: delay_type 1 means the value is relative to the free-running
     * clock, so re-base it onto the absolute clock.
     */
    if (dtype == 1) {
        TIME_$CLOCK(&current_clock);
        TIME_$ABS_CLOCK(&abs_clock);
        SUB48(&local_delay, &current_clock);
        ADD48(&local_delay, &abs_clock);
    }

    /* 0xE1657E */
    TIME_$ADVANCE(&dtype, &local_delay, &ec, &elem, &local_status);

    /*
     * 0xE1659A: on a scheduling failure the function returns immediately -
     * note that it does NOT run the elem.flags check at the bottom.
     */
    if (local_status != status_$ok) {
        *status = local_status;
        return;
    }

    /*
     * 0xE165A6..0xE165EE: wait for either our own eventcount to reach 1 or
     * the address space's quit eventcount to advance past its current value.
     *
     *   ecs  = { &ec, &FIM_$QUIT_EC[as], NULL }
     *   vals = { 1,   FIM_$QUIT_VALUE[as] + 1, 0 }
     *
     * Both arrays are pushed by value (24 bytes); EC_$WAIT returns the
     * 0-based index of the eventcount that was satisfied.
     */
    as_id = PROC1_$AS_ID;
    quit_value = (int32_t)FIM_$QUIT_VALUE[as_id] + 1;

    as_id = PROC1_$AS_ID;   /* 0xE165CC re-reads it for the ecs array */
    which = EC_$WAIT(
        (ec_$wait_ecs_t){ { &ec, &FIM_$QUIT_EC[as_id], NULL } },
        (ec_$wait_vals_t){ { 1, quit_value, 0 } });

    /* 0xE165F2: index 0 is our timer; anything else is the quit eventcount */
    if (which != 0) {
        /* 0xE165F6: TIME_$CANCEL's first argument is a by-value longword */
        TIME_$CANCEL(1, &elem, &local_status);
        *status = status_$time_quit_while_waiting;

        /* 0xE16610: consume the quit by latching the eventcount's value */
        as_id = PROC1_$AS_ID;
        FIM_$QUIT_VALUE[as_id] = (uint32_t)FIM_$QUIT_EC[as_id].value;
    }

    /*
     * 0xE16634: the timer callback marks the element in-use; finding it still
     * flagged here means the queue element was left in a bad state.
     */
    if (elem.flags != 0) {
        CRASH_SYSTEM(&wait_crash_status_00e16650);
    }
}
