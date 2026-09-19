/*
 * TIME_$INIT - Initialise the time subsystem
 *
 * Initialises the queue module, the real-time queue and the 64 per-process
 * virtual-timer queues, the two deferred-interrupt elements, optionally
 * seeds every clock cell from the calendar chip, and finally arms the
 * hardware timer.
 *
 * Parameters (frame 0x00E2FE74):
 *   0x08 read_calendar - Domain boolean by reference (`move.b (A0),D2b`,
 *                        tested with `tst.b D2b / bpl` at 0x00E2FEDA): true
 *                        (0xFF) = read the calendar and seed the clocks
 *
 * Original address: 0x00e2fe6c, 228 bytes
 *
 * The VT-queue walk (0x00E2FE94..0x00E2FEBA) starts A2 at 0xE29198 + 0xC
 * and passes `pea (0x12fc,A2)`, i.e. 0xE29198 + 0xC + 0x12FC = 0xE2A4A0 =
 * &TIME_$VTQ[0], stepping 0xC per queue; `moveq #0x3f,D3 / dbf` runs it 64
 * times with ids 1..64 in D4.  (0x1608,A0) = 0xE2A7A0 = TIME_$RTEQ.
 */

#include "time/time_internal.h"

void TIME_$INIT(uint8_t *read_calendar)
{
    int8_t do_read;         /* D2b */
    uint16_t queue_id;      /* D4w */
    int16_t count;          /* D3w */
    clock_t cal_clock;      /* A6-0xC */
    uint32_t cal_time;      /* A6-0x4 */

    /* 0x00E2FE74..0x00E2FE78 */
    do_read = (int8_t)*read_calendar;

    /* 0x00E2FE7A */
    TIME_$Q_INIT();

    /* 0x00E2FE80..0x00E2FE92: clr.l = {is_vt false, id 0}; queue = TIME_$RTEQ */
    TIME_$Q_INIT_QUEUE(0, 0, &TIME_$RTEQ);

    /*
     * 0x00E2FE94..0x00E2FEBA: 64 iterations, `st -(SP)` pushes is_vt true
     * (0xFF) in the high byte of the word slot, then the id word.
     */
    queue_id = 1;
    for (count = 0x3F; count >= 0; count--) {
        TIME_$Q_INIT_QUEUE((boolean)0xFF, queue_id, &TIME_$VTQ[queue_id - 1]);
        queue_id++;
    }

    /* 0x00E2FEBE..0x00E2FED8: 0xE2B10E, then 0xE2B11E */
    DI_$INIT_Q_ELEM(&TIME_$DI_VT);
    DI_$INIT_Q_ELEM(&TIME_$DI_RTE);

    /* 0x00E2FEDA: tst.b D2b / bpl.b 0x00e2ff38 */
    if (do_read < 0) {
        /* 0x00E2FEDE..0x00E2FEEC: TIME_$READ_CAL(&cal_clock, &cal_time) */
        TIME_$READ_CAL(&cal_clock, &cal_time);

        /* 0x00E2FEF2..0x00E2FF12 */
        TIME_$CLOCKH = cal_clock.high;              /* 0xE2B0D4 */
        TIME_$CLOCKL = cal_clock.low;               /* 0xE2B0E0 */
        TIME_$CURRENT_CLOCKH = cal_clock.high;      /* 0xE2B0E4 */
        TIME_$CURRENT_CLOCKL = cal_clock.low;       /* 0xE2B0E8 */
        TIME_$BOOT_TIME = cal_clock.high;           /* 0xE2B0EC */

        /* 0x00E2FF1A..0x00E2FF20: seconds rebased from the Apollo epoch to Unix */
        TIME_$CURRENT_TIME = cal_time + APOLLO_EPOCH_OFFSET;   /* 0xE2B0F0 */

        /* 0x00E2FF26..0x00E2FF32 */
        TIME_$CURRENT_USEC = 0;                     /* 0xE2B0F4 */
        TIME_$CURRENT_SKEW = 0;                     /* 0xE2B0FA */
        TIME_$CURRENT_DELTA = 0;                    /* 0xE2B0FC */
    }

    /* 0x00E2FF38: move.w #0x1047,(0x00e2b0f8).l */
    TIME_$CURRENT_TICK = TIME_INITIAL_TICK;

    /* 0x00E2FF40 */
    TIMER_$INIT();
}
