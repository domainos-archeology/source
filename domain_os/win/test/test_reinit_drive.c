/*
 * win/test/test_reinit_drive.c - win_$reinit_drive (0x00E194B4)
 *
 * Pins: the request cell is cleared around the work and restored; DISK_INIT
 * failure skips the rezero; otherwise mode 0x0A, ANSI REZERO with the
 * shared zero parameter, a wait of eventcount+1 / clock+0x28, and the
 * status of WIN_$CHECK_DISK_STATUS; the counter at +0x64 and the
 * head/cylinder reset happen on both paths.
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
uint16_t WIN_ANSI_IN_PARAM;

static uint8_t regs[0x10];
static status_$t init_status, cds_status;
static uint32_t req_during;
static int32_t *total_seen;
static uint16_t ansi_cmd_seen;
static char *ansi_in_seen;
static int nansi, nwait, ncds;
static ec_$wait_vals_t seen_vals;
static uint8_t mode_at_ansi;

status_$t DISK_INIT(uint16_t unit, uint16_t sub_unit, int32_t *total_blocks,
                    uint16_t *blocks_per_track, uint16_t *heads,
                    uint16_t *geometry, uint16_t *drive_id)
{
    (void)unit; (void)sub_unit; (void)blocks_per_track; (void)heads;
    (void)geometry; (void)drive_id;
    req_during = WIN_CUR_REQ_VA;
    total_seen = total_blocks;
    return init_status;
}
status_$t WIN_$ANSI_COMMAND(uint16_t unit, uint16_t cmd, char *in, char *out)
{
    (void)unit; (void)out;
    nansi++;
    ansi_cmd_seen = cmd;
    ansi_in_seen = in;
    mode_at_ansi = regs[WIN_REG_MODE];
    return 0x999;
}
int16_t EC_$WAIT(ec_$wait_ecs_t ecs, ec_$wait_vals_t vals)
{
    (void)ecs;
    nwait++;
    seen_vals = vals;
    return 0;
}
status_$t WIN_$CHECK_DISK_STATUS(uint16_t unit) { (void)unit; ncds++; return cds_status; }

#include "../reinit_drive.c"

static void reset(void)
{
    memset(WIN_$DATA.bytes, 0, sizeof(WIN_$DATA.bytes));
    memset(regs, 0, sizeof(regs));
    *(volatile uint8_t **)(WIN_UNIT(0) + WIN_BASE_ADDR_OFFSET) = regs;
    WIN_CUR_REQ_VA = 0x123456;
    WIN_$DATA.image.cur_head = 2;
    WIN_$DATA.image.cur_cyl = 100;
    WIN_UNIT_EC(0)->value = 0x40;
    TIME_$CLOCKH = 0x1000;
    init_status = 0;
    cds_status = 0;
    nansi = nwait = ncds = 0;
}

TEST(init_fails)
{
    reset();
    init_status = 0x80001;
    ASSERT_EQ(0x80001, win_$reinit_drive(0, 3));
    ASSERT_EQ(0, req_during);
    ASSERT_EQ((uintptr_t)(WIN_DATA_BASE + 0x68), (uintptr_t)total_seen);
    ASSERT_EQ(0, nansi);
    ASSERT_EQ(1, *(uint32_t *)(WIN_DATA_BASE + 0x64));
    ASSERT_EQ(-1, WIN_$DATA.image.cur_cyl);
    ASSERT_EQ(-1, WIN_$DATA.image.cur_head);
    ASSERT_EQ(0x123456, WIN_CUR_REQ_VA);
}

TEST(rezero_path)
{
    reset();
    cds_status = 0x80023;
    ASSERT_EQ(0x80023, win_$reinit_drive(0, 3));
    ASSERT_EQ(1, nansi);
    ASSERT_EQ(4, ansi_cmd_seen);
    ASSERT_EQ((uintptr_t)&WIN_ANSI_IN_PARAM, (uintptr_t)ansi_in_seen);
    ASSERT_EQ(0x0A, mode_at_ansi);
    ASSERT_EQ(1, nwait);
    ASSERT_EQ(0x41, seen_vals.val[0]);
    ASSERT_EQ(0x1028, seen_vals.val[1]);
    ASSERT_EQ(1, ncds);
    ASSERT_EQ(1, *(uint32_t *)(WIN_DATA_BASE + 0x64));
    ASSERT_EQ(0x123456, WIN_CUR_REQ_VA);
}

int main(void)
{
    printf("win_$reinit_drive tests:\n");
    RUN_TEST(init_fails);
    RUN_TEST(rezero_path);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
