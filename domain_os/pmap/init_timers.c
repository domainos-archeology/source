/*
 * PMAP_$INIT_TIMERS - Enter the purifier and update timers on the real-time queue
 *
 * 0x00E2F880 - 0x00E2F956 (216 bytes, the small second `PMAP_` code segment
 * at 0xE2F880).  Re-emitted from the disassembly 2026-09-27.  Wrong before:
 * the purifier interval was written as {0x7270E, 0}; the image clears the
 * WORD at element +0x14 and stores the LONGWORD 0x0007270E at +0x16
 * (0x00E2F8BA / 0x00E2F8BE), i.e. a 48-bit clock of {high 7, low 0x270E}.
 * The update timer's interval really is {0xE5, 0} (a longword at +0x14 and
 * a cleared word at +0x18, 0x00E2F918 / 0x00E2F920).
 *
 * A0 = 0xE24D44, the PMAP data block: the update timer element is the
 * block's first 0x1A bytes and the purifier element starts at +0x20
 * (0xE24D64).  Both fire first at TIME_$CLOCKH + 0xE5 ({clock+0xE5, 0}
 * at +0x0C/+0x10) with flags 0x1A / 0x16 at +0x12, and are entered on
 * TIME_$RTEQ (0xE2A7A0) with the `when' cell {TIME_$CLOCKH, 0} at
 * (-0xC,A6)/(-0x8,A6).  A non-zero status from either TIME_$Q_ENTER_ELEM
 * crashes with that status.
 *
 * Timer elements through PMAP_$DATA: Claude Opus 5.5 (source-iq58).
 */

#include "pmap/pmap_internal.h"
#include "misc/misc.h"

/* "movea.l #0xe24d44,A0": PMAP_$DATA +0x00 and +0x20 (source-iq58) */
#define PMAP_UPDATE_TIMER_ELEM   (&PMAP_$DATA.update_timer)
#define PMAP_PURIFIER_TIMER_ELEM (&PMAP_$DATA.purifier_timer)

/* 0x00E2F894 / 0x00E2F8FA: the two flag words */
#define PMAP_PURIFIER_TIMER_FLAGS   0x1A
#define PMAP_UPDATE_TIMER_FLAGS     0x16
/* 0x00E2F8AC: first firing, and the update timer's interval (0x00E2F918) */
#define PMAP_TIMER_FIRST_DELAY      0xE5
/* 0x00E2F8BE: the purifier interval, 0x0007270E over the 48-bit clock */
#define PMAP_PURIFIER_INTERVAL_HIGH 0x00000007u
#define PMAP_PURIFIER_INTERVAL_LOW  0x270Eu

void PMAP_$INIT_TIMERS(void)
{
    uint32_t now;               /* D2 */
    uint32_t first;             /* D3 */
    clock_t when;               /* (-0xC,A6) */
    status_$t status;           /* (-0x14,A6) */
    time_queue_elem_t *e;

    /* 0x00E2F88E - 0x00E2F8C2 */
    now = TIME_$CLOCKH;
    e = PMAP_PURIFIER_TIMER_ELEM;
    e->flags = PMAP_PURIFIER_TIMER_FLAGS;
    e->callback = ARCH_PTR_TO_VA(PMAP_$T_PURIF_CALLBACK);
    when.high = now;
    when.low = 0;
    first = now + PMAP_TIMER_FIRST_DELAY;
    e->expire_high = first;
    e->expire_low = 0;
    e->interval_high = PMAP_PURIFIER_INTERVAL_HIGH;     /* clr.w +0x14 / move.l +0x16 */
    e->interval_low = PMAP_PURIFIER_INTERVAL_LOW;

    /* 0x00E2F8C6 - 0x00E2F8F2 */
    TIME_$Q_ENTER_ELEM(&TIME_$RTEQ, &when, e, &status);
    if (status != status_$ok) {
        CRASH_SYSTEM(&status);
    }

    /* 0x00E2F8F4 - 0x00E2F920 */
    e = PMAP_UPDATE_TIMER_ELEM;
    e->flags = PMAP_UPDATE_TIMER_FLAGS;
    e->callback = ARCH_PTR_TO_VA(PMAP_$UPDATE_CALLBACK);
    when.high = now;
    when.low = 0;
    e->expire_high = first;
    e->expire_low = 0;
    e->interval_high = PMAP_TIMER_FIRST_DELAY;
    e->interval_low = 0;

    /* 0x00E2F924 - 0x00E2F94E */
    TIME_$Q_ENTER_ELEM(&TIME_$RTEQ, &when, e, &status);
    if (status != status_$ok) {
        CRASH_SYSTEM(&status);
    }
}
