/*
 * win/wait_for_controller.c - WAIT_FOR_CONTROLLER
 *
 * Original address: 0x00E190BC, 108 bytes.  Was FUN_00e190bc; renamed in
 * Ghidra as part of bead source-1nob.
 *
 * A module-internal routine of the WIN driver: it does not set up A5 itself
 * (0x00E190DC uses whatever A5 the caller left), it is reached with `bsr`,
 * and it returns its status in D0 with no Pascal result slot.
 *
 * All three callers store a command into the unit's register block and write
 * WIN_REG_GO, then call this to spin until the controller drops its busy bit
 * and to turn the resulting status word into a status code:
 *
 *   WIN_$ANSI_COMMAND  0x00E1916A
 *   SEEK               0x00E195D8
 *   DISK_INIT          0x00E199EE
 *
 * Every basic block of the original is accounted for; the addresses in the
 * comments say which instructions each statement stands for.
 */

#include "win/win_internal.h"

/* Read the drive's 16-bit status word (unit->regs + 0x06). */
#define WIN_STATUS_WORD(regs) (*(volatile uint16_t *)((regs) + WIN_REG_STATUS))

status_$t WAIT_FOR_CONTROLLER(uint16_t unit)
{
    volatile uint8_t *regs;
    int32_t deadline; /* D0 */
    status_$t status; /* D0 */
    uint16_t stat;    /* D3 */

    /*
     * 0x00E190C8-0x00E190D0: the timeout is three TIME_$CLOCKH ticks past
     * now (a tick is 65536 * 4 us, so a little under 0.8 s).  The deadline is
     * computed once, before the register block is even resolved.
     */
    deadline = (int32_t)WIN_CLOCKH() + 3;

    /* 0x00E190CE-0x00E190E0: A0 = WIN_UNITS[unit].regs */
    regs = WIN_UNIT_REGS(unit);

    /*
     * 0x00E190EC-0x00E190F4: spin while the status word is negative (busy),
     * giving up once TIME_$CLOCKH passes the deadline.  `cmp.l (A1),D0 / bge`
     * keeps looping while deadline >= TIME_$CLOCKH, so the clock is re-read
     * every time round.
     */
    while ((int16_t)WIN_STATUS_WORD(regs) < 0 &&
           deadline >= (int32_t)WIN_CLOCKH()) {
        /* nothing: the loop body is the two tests */
    }

    /*
     * 0x00E190F6-0x00E19104: still busy after the timeout is "disk
     * controller busy"; the test is made again rather than reusing the
     * loop's own condition.
     */
    if ((int16_t)WIN_STATUS_WORD(regs) < 0) {
        status = status_$disk_controller_busy; /* 0x00E19100 */
    } else {
        status = status_$ok; /* 0x00E190FC */
    }

    /*
     * 0x00E19106-0x00E1911C: `move.w (0x6,A0),D3w / tst.b D3b / bpl` tests
     * the LOW byte of the status word, i.e. bit 7.  A not-ready drive bumps
     * the statistics word, takes the command back down and overrides the
     * status just computed.
     */
    stat = WIN_STATUS_WORD(regs);
    if ((stat & WIN_STAT_NOT_READY) != 0) {
        WIN_NOT_READY_COUNT++;                  /* 0x00E1910E */
        *(regs + WIN_REG_GO) = 0;               /* 0x00E19112 */
        status = status_$disk_not_ready;        /* 0x00E19118 */
    }

    return status; /* D0 */
}
