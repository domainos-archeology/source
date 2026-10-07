/*
 * win/test/test_wait_for_controller.c - unit tests for WAIT_FOR_CONTROLLER
 * (0x00E190BC, was FUN_00e190bc)
 *
 * The real win/wait_for_controller.c is #included below and driven through a
 * fake unit record and register block.  TIME_$CLOCKH is a plain variable the
 * test advances by hand, which is what lets the spin loop terminate.
 */

#include <stdio.h>
#include <string.h>

#include "win/win_internal.h"

/* ------------------------------------------------------------------ */
/* Module data (normally win/win_data.c) and the globals WIN reaches   */
/* ------------------------------------------------------------------ */
MODULE_DATA_DEFINE(win_$data_t, WIN_$DATA, 0x00E2B89C);
ec_$eventcount_t TIME_$CLOCKH_EC = { .value = (int32_t)(0) };  /* TIME_$CLOCKH = its value */

/*
 * The host stand-in for the interrupt-advanced clock (see WIN_CLOCKH in
 * win/win_internal.h).  It returns TIME_$CLOCKH and then advances it by
 * clock_step, so a spin loop that polls it does terminate.
 */
static uint32_t clock_step;

uint32_t win_$host_clockh(void)
{
    uint32_t now = TIME_$CLOCKH;

    TIME_$CLOCKH += clock_step;
    return now;
}

/* The drive's register block; the unit record points at it. */
static uint8_t regs[0x10];

#include "../wait_for_controller.c"

/* ------------------------------------------------------------------ */
/* Tiny test harness                                                   */
/* ------------------------------------------------------------------ */
static int tests_run;
static int tests_failed;
static int current_failed;

#define CHECK_EQ(expected, actual)                                             \
    do {                                                                       \
        long long e_ = (long long)(expected);                                  \
        long long a_ = (long long)(actual);                                    \
        if (e_ != a_) {                                                        \
            current_failed = 1;                                                \
            printf("\n    FAIL line %d: %s: expected 0x%llx, got 0x%llx",      \
                   __LINE__, #actual, (unsigned long long)e_,                  \
                   (unsigned long long)a_);                                    \
        }                                                                      \
    } while (0)

#define RUN(fn)                                                                \
    do {                                                                       \
        tests_run++;                                                           \
        current_failed = 0;                                                    \
        printf("  %-40s", #fn);                                                \
        reset_state();                                                         \
        fn();                                                                  \
        if (current_failed) {                                                  \
            tests_failed++;                                                    \
            printf("\n  %-40s FAILED\n", #fn);                                 \
        } else {                                                               \
            printf(" ok\n");                                                   \
        }                                                                      \
    } while (0)

static void set_status(uint16_t v)
{
    *(uint16_t *)(regs + WIN_REG_STATUS) = v;
}

static void reset_state(void)
{
    memset(WIN_$DATA.bytes, 0, sizeof(WIN_$DATA.bytes));
    memset(regs, 0, sizeof(regs));
    TIME_$CLOCKH = 0;
    clock_step = 0;
    /* unit 0's record points at the fake register block */
    *(uint8_t **)(WIN_UNIT(0) + WIN_BASE_ADDR_OFFSET) = regs;
}

/* ------------------------------------------------------------------ */
/* Tests                                                               */
/* ------------------------------------------------------------------ */

/* 0x00E190EC-0x00E190FE: not busy and ready -> status_$ok */
static void test_ready(void)
{
    set_status(0x0800);
    regs[WIN_REG_GO] = 6;

    CHECK_EQ(status_$ok, WAIT_FOR_CONTROLLER(0));
    CHECK_EQ(0, WIN_NOT_READY_COUNT);
    CHECK_EQ(6, regs[WIN_REG_GO]); /* untouched when the drive is ready */
}

/*
 * 0x00E19106-0x00E1911C: bit 7 of the status word (the LOW byte, which is
 * what `tst.b D3b` looks at) is "drive not ready".  It bumps the statistics
 * word, clears WIN_REG_GO and overrides the status.
 */
static void test_not_ready(void)
{
    set_status(WIN_STAT_NOT_READY);
    regs[WIN_REG_GO] = 6;

    CHECK_EQ(status_$disk_not_ready, WAIT_FOR_CONTROLLER(0));
    CHECK_EQ(1, WIN_NOT_READY_COUNT);
    CHECK_EQ(0, regs[WIN_REG_GO]);
}

/*
 * The busy bit is bit 15, tested with `tst.w` / `bpl`, so only the sign of
 * the word matters.  With the clock already past the deadline the loop makes
 * no progress and the busy status is returned.
 */
static void test_busy_times_out(void)
{
    set_status(WIN_STAT_BUSY);
    TIME_$CLOCKH = 100; /* the deadline captured at 0x00E190C8 becomes 103 */
    clock_step = 1;     /* one tick per read, so the loop gives up */

    CHECK_EQ(status_$disk_controller_busy, WAIT_FOR_CONTROLLER(0));
    /* busy with bit 7 clear leaves the statistics word alone */
    CHECK_EQ(0, WIN_NOT_READY_COUNT);
    /* the deadline is 103 and the loop stops on the first read past it */
    CHECK_EQ(1, TIME_$CLOCKH > 103);
}

/*
 * A drive that is both busy at the deadline and not ready reports "not
 * ready": 0x00E19118 overwrites the 0x00E19100 status.
 */
static void test_busy_and_not_ready(void)
{
    set_status(WIN_STAT_BUSY | WIN_STAT_NOT_READY);
    clock_step = 1;

    CHECK_EQ(status_$disk_not_ready, WAIT_FOR_CONTROLLER(0));
    CHECK_EQ(1, WIN_NOT_READY_COUNT);
}

/* The unit index scales the record by 12 bytes (0x00E190D2-0x00E190DC). */
static void test_unit_indexing(void)
{
    static uint8_t regs2[0x10];

    memset(regs2, 0, sizeof(regs2));
    *(uint8_t **)(WIN_UNIT(2) + WIN_BASE_ADDR_OFFSET) = regs2;
    *(uint16_t *)(regs2 + WIN_REG_STATUS) = WIN_STAT_NOT_READY;
    regs2[WIN_REG_GO] = 5;

    /* unit 0 is ready, unit 2 is not */
    set_status(0);
    CHECK_EQ(status_$ok, WAIT_FOR_CONTROLLER(0));
    CHECK_EQ(status_$disk_not_ready, WAIT_FOR_CONTROLLER(2));
    CHECK_EQ(0, regs2[WIN_REG_GO]);
    CHECK_EQ(1, WIN_NOT_READY_COUNT);
}

int main(void)
{
    setvbuf(stdout, NULL, _IOLBF, 0);
    printf("WAIT_FOR_CONTROLLER (0x00E190BC) tests\n");

    RUN(test_ready);
    RUN(test_not_ready);
    RUN(test_busy_times_out);
    RUN(test_busy_and_not_ready);
    RUN(test_unit_indexing);

    printf("%d tests, %d failed\n", tests_run, tests_failed);
    return tests_failed != 0;
}
