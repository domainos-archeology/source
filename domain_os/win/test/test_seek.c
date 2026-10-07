/*
 * win/test/test_seek.c - SEEK (0x00E19576)
 *
 * Pins: head select only when the head differs (go 8, then the controller
 * wait and the status check); no seek when the cylinder matches; a started
 * seek returns -1 without waiting; a waited seek checks status and records
 * the cylinder; retryable errors reinit (waiting) or give seek-error (not
 * waiting); other errors retry once; the clock timeout path.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    int _before = tests_failed; \
    printf("  Running %s... ", #name); \
    fflush(stdout); \
    test_##name(); \
    if (tests_failed == _before) { tests_passed++; printf("PASSED\n"); } \
} while (0)

#define ASSERT_EQ(expected, actual) do { \
    unsigned long _e = (unsigned long)(expected); \
    unsigned long _a = (unsigned long)(actual); \
    if (_e != _a) { \
        printf("FAILED\n    Expected: 0x%lx, Got: 0x%lx at line %d\n", \
               _e, _a, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while (0)

#include "win/win_internal.h"

MODULE_DATA_DEFINE(win_$data_t, WIN_$DATA, 0x00E2B89C);
uint32_t win_$host_clockh(void) { return 0; }
ec_$eventcount_t TIME_$CLOCKH_EC = { .value = (int32_t)(0) };  /* TIME_$CLOCKH = its value */

static uint8_t regs[0x10];
static status_$t wfc_status;
static status_$t cds_script[8];
static int ncds, nwfc, nreinit, nwait;
static int16_t wait_ret;
static ec_$wait_vals_t seen_vals;
static uint8_t go_seen[8];
static int ngo;

status_$t WAIT_FOR_CONTROLLER(uint16_t unit)
{
    (void)unit;
    go_seen[ngo++] = regs[WIN_REG_GO];
    nwfc++;
    return wfc_status;
}
status_$t WIN_$CHECK_DISK_STATUS(uint16_t unit) { (void)unit; return cds_script[ncds++]; }
status_$t win_$reinit_drive(uint16_t unit, uint16_t dev_unit)
{
    (void)unit; (void)dev_unit;
    nreinit++;
    WIN_$DATA.image.cur_cyl = -1;
    WIN_$DATA.image.cur_head = -1;
    return 0;
}
int16_t EC_$WAIT(ec_$wait_ecs_t ecs, ec_$wait_vals_t vals)
{
    (void)ecs;
    nwait++;
    seen_vals = vals;
    return wait_ret;
}

#include "../seek.c"

static win_$request_t req;

static void reset(void)
{
    memset(WIN_$DATA.bytes, 0, sizeof(WIN_$DATA.bytes));
    memset(regs, 0, sizeof(regs));
    memset(cds_script, 0, sizeof(cds_script));
    *(volatile uint8_t **)(WIN_UNIT(0) + WIN_BASE_ADDR_OFFSET) = regs;
    WIN_$DATA.image.cur_head = 2;
    WIN_$DATA.image.cur_cyl = 100;
    WIN_UNIT_EC(0)->value = 0x40;
    TIME_$CLOCKH = 0x1000;
    wfc_status = 0;
    ncds = nwfc = nreinit = nwait = ngo = 0;
    wait_ret = 0;
    memset(&req, 0, sizeof(req));
    req.cylinder = 100;
    req.head = 2;
}

TEST(already_there)
{
    reset();
    ASSERT_EQ(0, SEEK(0, 0, &req, 0));
    ASSERT_EQ(0, nwfc);
    ASSERT_EQ(0, nwait);
}

TEST(head_select_then_same_cylinder)
{
    reset();
    req.head = 5;
    ASSERT_EQ(0, SEEK(0, 0, &req, 0));
    ASSERT_EQ(5, regs[WIN_REG_HEAD]);
    ASSERT_EQ(8, go_seen[0]);
    ASSERT_EQ(1, ncds);
    ASSERT_EQ(5, WIN_$DATA.image.cur_head);
}

TEST(seek_started_not_waiting)
{
    reset();
    req.cylinder = 7;
    ASSERT_EQ((uint32_t)-1, (uint32_t)SEEK(0, 0, &req, 0));
    ASSERT_EQ(7, *(uint16_t *)(regs + WIN_REG_CYLINDER));
    ASSERT_EQ(0x0A, regs[WIN_REG_MODE]);
    ASSERT_EQ(4, regs[WIN_REG_GO]);
    ASSERT_EQ(0xFF, WIN_DATA_BASE[WIN_FLAG_OFFSET]);
    ASSERT_EQ(0, nwait);
    ASSERT_EQ(100, WIN_$DATA.image.cur_cyl);
}

TEST(seek_waited_ok)
{
    reset();
    req.cylinder = 7;
    ASSERT_EQ(0, SEEK(0, 0, &req, 0xFF));
    ASSERT_EQ(1, nwait);
    ASSERT_EQ(0x41, seen_vals.val[0]);
    ASSERT_EQ(0x1008, seen_vals.val[1]);
    ASSERT_EQ(0x00, WIN_DATA_BASE[WIN_FLAG_OFFSET]);
    ASSERT_EQ(7, WIN_$DATA.image.cur_cyl);
}

TEST(not_ready_waiting_reinit_then_ok)
{
    reset();
    req.cylinder = 7;
    cds_script[0] = 0x80001;        /* after the first seek */
    cds_script[1] = 0;              /* head reselect after reinit */
    cds_script[2] = 0;              /* second seek */
    ASSERT_EQ(0, SEEK(0, 0, &req, 0xFF));
    ASSERT_EQ(1, nreinit);
    ASSERT_EQ(2, nwait);
    ASSERT_EQ(7, WIN_$DATA.image.cur_cyl);
}

TEST(not_ready_not_waiting_seek_error)
{
    reset();
    req.head = 3;
    wfc_status = 0x80005;
    ASSERT_EQ(status_$disk_seek_error, SEEK(0, 0, &req, 0));
    ASSERT_EQ(0, nreinit);
}

TEST(other_error_retries_once)
{
    reset();
    req.head = 3;
    wfc_status = 0x80009;
    ASSERT_EQ(0x80009, SEEK(0, 0, &req, 0));
    ASSERT_EQ(2, nwfc);
    ASSERT_EQ(0, nreinit);
}

TEST(timeout_twice_returns_ok_uncached)
{
    reset();
    req.cylinder = 7;
    wait_ret = 1;
    ASSERT_EQ(0, SEEK(0, 0, &req, 0xFF));
    ASSERT_EQ(2, nreinit);
    ASSERT_EQ(-1, WIN_$DATA.image.cur_cyl);
}

int main(void)
{
    printf("SEEK tests:\n");
    RUN_TEST(already_there);
    RUN_TEST(head_select_then_same_cylinder);
    RUN_TEST(seek_started_not_waiting);
    RUN_TEST(seek_waited_ok);
    RUN_TEST(not_ready_waiting_reinit_then_ok);
    RUN_TEST(not_ready_not_waiting_seek_error);
    RUN_TEST(other_error_retries_once);
    RUN_TEST(timeout_twice_returns_ok_uncached);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
