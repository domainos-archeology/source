/*
 * PROC1_$INIT_LOADAV - Clear the load averages and start their timer
 * Original address: 0x00e14c94 (136 bytes)
 *
 * Re-emitted from the disassembly.  A5 = 0x00E254E8 (the PROC1_ data
 * block): (0,A5)..(0xB,A5) are PROC1_$LOADAV[3] and (0x10,A5) is the
 * time_queue_elem_t PROC1_$LOADAV_ELEM, so (0x14,A5) is its callback,
 * (0x18,A5) callback_arg, (0x1C,A5)/(0x20,A5) expire, (0x22,A5) flags and
 * (0x24,A5)/(0x28,A5) interval.
 *
 * Locals: (-0x14,A6) a 6-byte clock (the interval, then the expiry),
 * (-0xC,A6) status, (-0x8,A6) the current clock.
 *
 * 0x00E14C94  link.w A6,-0x14 / pea (A5) / lea A5
 * 0x00E14CA0  PROC1_$LOADAV[0..2] = 0
 * 0x00E14CAA  elem.flags = 2                       TIME_QELEM_REPEAT
 * 0x00E14CB0  elem.callback = PROC1_$LOADAV_CALLBACK (0x00E14BDA)
 * 0x00E14CB8  elem.callback_arg = 0
 * 0x00E14CBC  clr.w (-0x14,A6) / move.l #0x1312d0,(-0x12,A6)
 *             -> interval clock high 0x00000013, low 0x12D0
 *             (0x1312D0 = 1,250,000 ticks = 5 s at 250,000/s)
 * 0x00E14CC8  elem.interval = that clock
 * 0x00E14CD4  TIME_$CLOCK(&now)                    (addq #4)
 * 0x00E14CE0  ADD48(&interval, &now)               (addq #8): interval += now
 * 0x00E14CF0  elem.expire = the sum
 * 0x00E14CFC  TIME_$Q_ENTER_ELEM(&TIME_$RTEQ, &now, &elem, &status)
 *             (`move.l #0xe2a7a0'; no cleanup: unlk)
 * 0x00E14D14  movea.l (-0x18,A6),A5 / unlk / rts
 *
 * Nothing looks at the status.  Only caller: OS_$INIT (0x00E342DC).
 */

#include "proc1/proc1_internal.h"
#include "time/time.h"
#include "cal/cal.h"

/* 0x00E14CBC / 0x00E14CC0: the 5-second repeat interval, as a 48-bit clock */
#define PROC1_LOADAV_INTERVAL_HIGH  0x00000013u
#define PROC1_LOADAV_INTERVAL_LOW   0x12D0u

void PROC1_$INIT_LOADAV(void)
{
    clock_t when;               /* (-0x14,A6) */
    status_$t status;           /* (-0xC,A6) */
    clock_t now;                /* (-0x8,A6) */

    /* 0x00E14CA0..0x00E14CA6 */
    PROC1_$LOADAV[0] = 0;
    PROC1_$LOADAV[1] = 0;
    PROC1_$LOADAV[2] = 0;

    /* 0x00E14CAA */
    PROC1_$LOADAV_ELEM.flags = TIME_QELEM_REPEAT;

    /* 0x00E14CB0 / 0x00E14CB8 */
    PROC1_$LOADAV_ELEM.callback = (uint32_t)(uintptr_t)PROC1_$LOADAV_CALLBACK;
    PROC1_$LOADAV_ELEM.callback_arg = 0;

    /* 0x00E14CBC / 0x00E14CC0 */
    when.high = PROC1_LOADAV_INTERVAL_HIGH;
    when.low = PROC1_LOADAV_INTERVAL_LOW;

    /* 0x00E14CC8 / 0x00E14CCE */
    PROC1_$LOADAV_ELEM.interval_high = when.high;
    PROC1_$LOADAV_ELEM.interval_low = when.low;

    /* 0x00E14CD4 */
    TIME_$CLOCK(&now);

    /* 0x00E14CE0..0x00E14CE8: when += now */
    ADD48(&when, &now);

    /* 0x00E14CF0 / 0x00E14CF6 */
    PROC1_$LOADAV_ELEM.expire_high = when.high;
    PROC1_$LOADAV_ELEM.expire_low = when.low;

    /* 0x00E14CFC..0x00E14D0E */
    TIME_$Q_ENTER_ELEM(&TIME_$RTEQ, &now, &PROC1_$LOADAV_ELEM, &status);
}
