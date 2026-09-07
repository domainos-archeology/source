/*
 * win/test/test_format_track.c - unit tests for WIN_$FORMAT_TRACK
 * (0x00E196AA, was FUN_00e196aa; bead source-aiy0).
 *
 * The real win/format_track.c is #included below; SEEK, WIN_$CHECK_DISK_STATUS
 * and EC_$WAIT are stubbed here and record what they were handed.
 *
 * The behaviours covered are the ones the reversing pass established:
 *   - the routine takes TWO arguments (0x00E196B2 / 0x00E196B6); the tree
 *     declared it with one, so the request pointer was read off the caller's
 *     stack
 *   - the cylinder word comes from the DEVICE entry at +0x1C, and the request
 *     is what SEEK is handed (0x00E196E0 / 0x00E196E6)
 *   - SEEK's fourth argument is the Domain boolean TRUE, `st -(SP)`
 *     (0x00E196E4), where WIN_$DO_IO's own seek passes zero
 *   - it retries five times, `moveq #0x4,D2` + `dbf` (0x00E196C8 / 0x00E19744)
 *   - the controller sequence is status:=0, mode:=9, go:=3
 *     (0x00E196FC / 0x00E1970C / 0x00E19712)
 *   - the eventcount wait is (win_ec, TIME_$CLOCKH, NULL) against
 *     (ec+1, clock+0x28, 0) and its result is DISCARDED (0x00E19718-0x00E19730)
 *   - on persistent failure the request's volume is invalidated at
 *     table + 0x1C*volume - 4 and the status lands in request +0x0C
 */

#include <stdio.h>
#include <string.h>

#include "win/win_internal.h"

/* ------------------------------------------------------------------ */
/* Module data and the globals WIN reaches                             */
/* ------------------------------------------------------------------ */
uint8_t  WIN_$DATA[WIN_DATA_SIZE];
uint8_t  WIN_$VOLUME_TABLE[WIN_VOLUME_TABLE_SIZE];
uint32_t TIME_$CLOCKH;

static uint8_t regs[0x10];

/* ------------------------------------------------------------------ */
/* Stubs                                                               */
/* ------------------------------------------------------------------ */
static int       seek_calls;
static uint16_t  seek_unit[8];
static uint16_t  seek_cyl[8];
static void     *seek_req[8];
static uint8_t   seek_flags[8];
static status_$t seek_result[8];

status_$t SEEK(uint16_t unit, uint16_t cylinder, void *req, uint8_t flags)
{
    status_$t st = status_$ok;

    if (seek_calls < 8) {
        seek_unit[seek_calls]  = unit;
        seek_cyl[seek_calls]   = cylinder;
        seek_req[seek_calls]   = req;
        seek_flags[seek_calls] = flags;
        st = seek_result[seek_calls];
    }
    seek_calls++;
    return st;
}

static int       check_calls;
static uint16_t  check_unit[8];
static status_$t check_result[8];

status_$t WIN_$CHECK_DISK_STATUS(uint16_t unit)
{
    status_$t st = status_$ok;

    if (check_calls < 8) {
        check_unit[check_calls] = unit;
        st = check_result[check_calls];
    }
    check_calls++;
    return st;
}

static int      wait_calls;
static void    *wait_ec0[8];
static void    *wait_ec1[8];
static void    *wait_ec2[8];
static int32_t  wait_v0[8];
static int32_t  wait_v1[8];
static int32_t  wait_v2[8];
/* The register state observed at the moment of the wait. */
static uint8_t  wait_regs_status[8];
static uint8_t  wait_regs_mode[8];
static uint8_t  wait_regs_go[8];

int16_t EC_$WAIT(ec_$wait_ecs_t ecs, ec_$wait_vals_t vals)
{
    if (wait_calls < 8) {
        wait_ec0[wait_calls] = ecs.ec[0];
        wait_ec1[wait_calls] = ecs.ec[1];
        wait_ec2[wait_calls] = ecs.ec[2];
        wait_v0[wait_calls]  = vals.val[0];
        wait_v1[wait_calls]  = vals.val[1];
        wait_v2[wait_calls]  = vals.val[2];
        wait_regs_status[wait_calls] = regs[WIN_REG_STATUS];
        wait_regs_mode[wait_calls]   = regs[WIN_REG_MODE];
        wait_regs_go[wait_calls]     = regs[WIN_REG_GO];
    }
    wait_calls++;
    /* A non-zero result would be "the clock fired"; the code must ignore it. */
    return 1;
}

/* The code under test, for real. */
#include "../format_track.c"

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

#define CHECK_PTR(expected, actual)                                            \
    do {                                                                       \
        if ((const void *)(expected) != (const void *)(actual)) {              \
            current_failed = 1;                                                \
            printf("\n    FAIL line %d: %s: pointer mismatch", __LINE__,       \
                   #actual);                                                   \
        }                                                                      \
    } while (0)

#define RUN(fn)                                                                \
    do {                                                                       \
        printf("  %-52s", #fn);                                                \
        current_failed = 0;                                                    \
        setup();                                                               \
        fn();                                                                  \
        tests_run++;                                                           \
        if (current_failed) { tests_failed++; printf("\n  ... FAIL\n"); }       \
        else { printf("ok\n"); }                                               \
    } while (0)

/* ------------------------------------------------------------------ */
/* Fixture                                                             */
/* ------------------------------------------------------------------ */
static uint8_t        dev_entry[0x20];
static win_$request_t req;

static void setup(void)
{
    int i;

    memset(WIN_$DATA, 0, sizeof(WIN_$DATA));
    memset(WIN_$VOLUME_TABLE, 0xAA, sizeof(WIN_$VOLUME_TABLE));
    memset(regs, 0, sizeof(regs));
    memset(dev_entry, 0, sizeof(dev_entry));
    memset(&req, 0, sizeof(req));

    seek_calls = check_calls = wait_calls = 0;
    for (i = 0; i < 8; i++) {
        seek_result[i]  = status_$ok;
        check_result[i] = status_$ok;
    }

    TIME_$CLOCKH = 0x1000;

    /* unit 0's register block lives at WIN data + 0x04 */
    *(volatile uint8_t **)(WIN_DATA_BASE + WIN_BASE_ADDR_OFFSET) = regs;

    /* unit 0's eventcount at WIN data + 0x30 */
    WIN_UNIT_EC(0)->value = 0x55;

    /* the cylinder word the device entry carries at +0x1C */
    *(uint16_t *)(dev_entry + 0x1C) = 0x0321;

    req.volume = 3;
    req.status = status_$ok;

    /* the request the driver thinks it is working on, to see it cleared */
    WIN_CUR_REQ = (void *)&req;
}

/* ------------------------------------------------------------------ */
/* Tests                                                               */
/* ------------------------------------------------------------------ */

/*
 * The happy path: one seek, one command sequence, one wait, one status check.
 * 0x00E196BE `clr.l (0x60,A5)` drops the current request before anything else.
 */
static void test_success_runs_exactly_one_pass(void)
{
    WIN_$FORMAT_TRACK(dev_entry, &req);

    CHECK_PTR(NULL, WIN_CUR_REQ);
    CHECK_EQ(1, seek_calls);
    CHECK_EQ(1, wait_calls);
    CHECK_EQ(1, check_calls);
    CHECK_EQ(status_$ok, req.status);
    /* the volume byte must be untouched on success */
    CHECK_EQ(0xAA, WIN_VOLUME_MOUNTED(req.volume));
}

/*
 * SEEK's arguments: unit 0, the DEVICE entry's cylinder word, the request
 * pointer, and the Domain boolean TRUE.  The second argument is the thing the
 * one-parameter declaration in win_internal.h could not supply.
 */
static void test_seek_arguments(void)
{
    WIN_$FORMAT_TRACK(dev_entry, &req);

    CHECK_EQ(0, seek_unit[0]);
    CHECK_EQ(0x0321, seek_cyl[0]);
    CHECK_PTR(&req, seek_req[0]);
    CHECK_EQ(0xFF, seek_flags[0]);
    CHECK_EQ(0, check_unit[0]);
}

/*
 * 0x00E196FC `move.b #0x0,(0x6,A2)`, 0x00E1970C `move.b #0x9,(0xc,A2)`,
 * 0x00E19712 `move.b #0x3,(0xe,A2)` - all three are in place by the time the
 * wait starts.
 */
static void test_controller_command_sequence(void)
{
    regs[WIN_REG_STATUS] = 0x77;

    WIN_$FORMAT_TRACK(dev_entry, &req);

    CHECK_EQ(0x00, wait_regs_status[0]);
    CHECK_EQ(WIN_MODE_FORMAT, wait_regs_mode[0]);
    CHECK_EQ(WIN_GO_FORMAT, wait_regs_go[0]);
}

/*
 * 0x00E19718-0x00E1972A: ecs = {win_ec, &TIME_$CLOCKH, NULL} and
 * vals = {win_ec->value + 1, TIME_$CLOCKH + 0x28, 0}.  The eventcount value
 * is sampled BEFORE the command is issued (0x00E19702, ahead of 0x00E1970C).
 */
static void test_eventcount_wait_arguments(void)
{
    WIN_$FORMAT_TRACK(dev_entry, &req);

    CHECK_PTR(WIN_UNIT_EC(0), wait_ec0[0]);
    CHECK_PTR(&TIME_$CLOCKH, wait_ec1[0]);
    CHECK_PTR(NULL, wait_ec2[0]);
    CHECK_EQ(0x56, wait_v0[0]);
    CHECK_EQ(0x1028, wait_v1[0]);
    CHECK_EQ(0, wait_v2[0]);
}

/*
 * A failing status check retries; the loop is `moveq #0x4,D2` + `dbf`, so
 * five passes and no more (0x00E196C8 / 0x00E19744).
 */
static void test_five_attempts_then_gives_up(void)
{
    int i;

    for (i = 0; i < 8; i++) {
        check_result[i] = status_$disk_not_ready;
    }

    WIN_$FORMAT_TRACK(dev_entry, &req);

    CHECK_EQ(5, seek_calls);
    CHECK_EQ(5, check_calls);
    CHECK_EQ(status_$disk_not_ready, req.status);
    /* entry (volume - 1) + 0x18 == table + 0x1C*volume - 4 */
    CHECK_EQ(0x00, WIN_VOLUME_TABLE[3 * WIN_VOLUME_ENTRY_SIZE - 4]);
    CHECK_EQ(0xAA, WIN_VOLUME_TABLE[2 * WIN_VOLUME_ENTRY_SIZE - 4]);
}

/*
 * A failing SEEK skips the whole command sequence (0x00E196FA `bne` straight
 * to the dbf) but still burns an attempt, and the SEEK status is what the
 * request ends up carrying.
 */
static void test_seek_failure_skips_the_command_and_retries(void)
{
    int i;

    for (i = 0; i < 8; i++) {
        seek_result[i] = status_$disk_seek_error;
    }

    WIN_$FORMAT_TRACK(dev_entry, &req);

    CHECK_EQ(5, seek_calls);
    CHECK_EQ(0, wait_calls);
    CHECK_EQ(0, check_calls);
    CHECK_EQ(status_$disk_seek_error, req.status);
    CHECK_EQ(0x00, WIN_VOLUME_MOUNTED(req.volume));
}

/*
 * Recovery on a later pass leaves the volume alone and the request status at
 * whatever it was: 0x00E19742 `beq` returns before the failure epilogue.
 */
static void test_recovery_on_the_third_pass(void)
{
    check_result[0] = status_$disk_not_ready;
    check_result[1] = status_$disk_not_ready;
    check_result[2] = status_$ok;

    WIN_$FORMAT_TRACK(dev_entry, &req);

    CHECK_EQ(3, seek_calls);
    CHECK_EQ(3, check_calls);
    CHECK_EQ(status_$ok, req.status);
    CHECK_EQ(0xAA, WIN_VOLUME_MOUNTED(req.volume));
}

/*
 * EC_$WAIT's return value is dropped (0x00E19730 `lea (0x18,SP),SP` with no
 * test of D0): the drive status alone decides.  The stub always returns 1.
 */
static void test_ec_wait_result_is_ignored(void)
{
    WIN_$FORMAT_TRACK(dev_entry, &req);

    CHECK_EQ(1, check_calls);
    CHECK_EQ(status_$ok, req.status);
}

int main(void)
{
    printf("WIN_$FORMAT_TRACK tests\n");

    RUN(test_success_runs_exactly_one_pass);
    RUN(test_seek_arguments);
    RUN(test_controller_command_sequence);
    RUN(test_eventcount_wait_arguments);
    RUN(test_five_attempts_then_gives_up);
    RUN(test_seek_failure_skips_the_command_and_retries);
    RUN(test_recovery_on_the_third_pass);
    RUN(test_ec_wait_result_is_ignored);

    printf("%d run, %d failed\n", tests_run, tests_failed);
    return tests_failed ? 1 : 0;
}
