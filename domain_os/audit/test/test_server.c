/*
 * audit/test/test_server.c - unit tests for AUDIT_$SERVER (0x00E710C6).
 *
 * audit/server.c is #included below with EC_$WAITN, the ML exclusion pair,
 * ACL_$ENTER_SUPER / ACL_$EXIT_SUPER, FILE_$FW_FILE and PROC1_$UNBIND mocked.
 * The point of the file is bead source-79jr: the image hands EC_$WAITN two
 * eventcounts, {AUDIT_$DATA.event_count, &TIME_$CLOCKH}, and two wait values,
 * {ec->value + 1, timeout}, in real arrays at A6-0x20 and A6-0x10.
 */

#include <stdio.h>
#include <string.h>

static int tests_passed = 0;
static int tests_failed = 0;
static int current_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do {                     \
    printf("  Running %s... ", #name);          \
    current_failed = 0;                         \
    test_##name();                              \
    if (current_failed) { tests_failed++; }     \
    else { tests_passed++; printf("PASSED\n"); }\
} while (0)

#define ASSERT_EQ(expected, actual) do {                                 \
    unsigned long long _e = (unsigned long long)(expected);              \
    unsigned long long _a = (unsigned long long)(actual);                \
    if (_e != _a) {                                                      \
        printf("FAILED\n    Expected 0x%llx, got 0x%llx at line %d\n",   \
               _e, _a, __LINE__);                                        \
        current_failed = 1;                                              \
        return;                                                          \
    }                                                                    \
} while (0)

#define ASSERT_TRUE(cond) do {                                           \
    if (!(cond)) {                                                       \
        printf("FAILED\n    %s at line %d\n", #cond, __LINE__);          \
        current_failed = 1;                                              \
        return;                                                          \
    }                                                                    \
} while (0)

#include "audit/audit_internal.h"
#include "acl/acl.h"
#include "time/time.h"
#include "file/file.h"

/* ------------------------------------------------------------------ */
/* Globals                                                             */
/* ------------------------------------------------------------------ */

audit_data_t AUDIT_$DATA;
int8_t       AUDIT_$ENABLED;
uint16_t     PROC1_$CURRENT;
ec_$eventcount_t TIME_$CLOCKH_EC = { .value = (int32_t)(0) };  /* TIME_$CLOCKH = its value */
uid_t        UID_$NIL = { 0, 0 };

/* ------------------------------------------------------------------ */
/* Mocks                                                              */
/* ------------------------------------------------------------------ */

static uint8_t wired_area[64];

static int   excl_start_calls;
static int   excl_stop_calls;
static void *excl_last_arg;

void ML_$EXCLUSION_START(ml_$exclusion_t *excl)
{
    excl_start_calls++;
    excl_last_arg = excl;
}

void ML_$EXCLUSION_STOP(ml_$exclusion_t *excl)
{
    excl_stop_calls++;
    excl_last_arg = excl;
}

void ACL_$ENTER_SUPER(void) { }
void ACL_$EXIT_SUPER(void)  { }

static int       fw_calls;
static uid_t     fw_uid;
void FILE_$FW_FILE(uid_t *file_uid, status_$t *status_ret)
{
    fw_calls++;
    fw_uid = *file_uid;
    *status_ret = status_$ok;
}

static int      unbind_calls;
static uint16_t unbind_pid;
void PROC1_$UNBIND(uint16_t pid, status_$t *status_ret)
{
    unbind_calls++;
    unbind_pid = pid;
    *status_ret = status_$ok;
}

/* EC_$WAITN capture: the arrays it is handed, and what it returns. */
static int                waitn_calls;
static ec_$eventcount_t  *waitn_ecs[2];
static int32_t            waitn_vals[2];
static int16_t            waitn_count;
static long               waitn_array_gap;   /* values - ecs, in bytes */
static uint16_t           waitn_result;

uint16_t EC_$WAITN(ec_$eventcount_t **ecs, int32_t *wait_val, int16_t num_ecs)
{
    waitn_calls++;
    waitn_ecs[0] = ecs[0];
    waitn_ecs[1] = ecs[1];
    waitn_vals[0] = wait_val[0];
    waitn_vals[1] = wait_val[1];
    waitn_count = num_ecs;
    waitn_array_gap = (long)((const uint8_t *)wait_val - (const uint8_t *)ecs);

    /* One turn of the loop only. */
    AUDIT_$ENABLED = 0;
    return waitn_result;
}

#include "../server.c"

/* ------------------------------------------------------------------ */

#define TEST_PID 4

static void run_server(uint16_t flags, int16_t timeout, uint16_t wake)
{
    memset(&AUDIT_$DATA, 0, sizeof(AUDIT_$DATA));
    memset(wired_area, 0, sizeof(wired_area));

    AUDIT_$DATA.event_count = (ec_$eventcount_t *)wired_area;
    AUDIT_$DATA.event_count->value = 0x1234;
    AUDIT_$DATA.flags = flags;
    AUDIT_$DATA.timeout = timeout;
    AUDIT_$DATA.server_pid = 9;

    TIME_$CLOCKH = 0x00010000u;
    PROC1_$CURRENT = TEST_PID;
    AUDIT_$ENABLED = (int8_t)0xFF;

    excl_start_calls = excl_stop_calls = 0;
    excl_last_arg = NULL;
    fw_calls = 0;
    unbind_calls = 0;
    waitn_calls = 0;
    memset(waitn_ecs, 0, sizeof(waitn_ecs));
    memset(waitn_vals, 0, sizeof(waitn_vals));
    waitn_count = -1;
    waitn_array_gap = 0;
    waitn_result = wake;

    AUDIT_$SERVER();
}

/*
 * 0x00E710E6 / 0x00E710EC: both eventcount slots are filled before the loop.
 * 0x00E7116C-0x00E71176: wait_val[0] = *event_count + 1.
 */
TEST(waitn_gets_two_eventcounts)
{
    run_server(0, 0, 1);

    ASSERT_EQ(1, waitn_calls);
    ASSERT_TRUE(waitn_ecs[0] == (ec_$eventcount_t *)wired_area);
    ASSERT_TRUE(waitn_ecs[1] == (ec_$eventcount_t *)&TIME_$CLOCKH);
    ASSERT_EQ(0x1235, waitn_vals[0]);
    /* Both arrays exist as real objects; the mock read slot 1 of each,
     * which is only defined because the caller declares two elements.
     * (The 0x10-byte A6-0x20 / A6-0x10 gap is an m68k frame detail the host
     * compiler is free to lay out differently.) */
    ASSERT_TRUE(waitn_array_gap != 0);
    /* Reading ecs[1] and wait_val[1] in the mock is only defined because the
     * caller really declares two-element arrays. */
}

/* 0x00E71132: `btst.l #0x1` clear -> the count stays 1. */
TEST(count_is_one_without_the_timeout_flag)
{
    run_server(0, 0, 1);
    ASSERT_EQ(1, waitn_count);
}

/* 0x00E7114A: timeout word zero -> TIME_$CLOCKH + 0x1E0, count 2. */
TEST(default_timeout)
{
    run_server(AUDIT_FLAG_TIMEOUT, 0, 1);
    ASSERT_EQ(2, waitn_count);
    ASSERT_EQ(0x000101E0, waitn_vals[1]);
}

/* 0x00E7113E: `ext.l` + `lsl.l #0x2` -> TIME_$CLOCKH + timeout*4. */
TEST(configured_timeout)
{
    run_server(AUDIT_FLAG_TIMEOUT, 100, 1);
    ASSERT_EQ(2, waitn_count);
    ASSERT_EQ(0x00010000 + 400, waitn_vals[1]);
}

/* 0x00E711A2-0x00E711D0: wake 2, a live log file and a dirty buffer. */
TEST(timeout_wake_flushes_dirty_buffer)
{
    run_server(AUDIT_FLAG_TIMEOUT, 0, 2);
    ASSERT_EQ(0, fw_calls);   /* log_file_uid is still NIL */

    memset(&AUDIT_$DATA, 0, sizeof(AUDIT_$DATA));
    AUDIT_$DATA.event_count = (ec_$eventcount_t *)wired_area;
    AUDIT_$DATA.log_file_uid.high = 0xAABBCCDDu;
    AUDIT_$DATA.dirty = (int8_t)0xFF;
    AUDIT_$DATA.server_pid = 9;
    AUDIT_$ENABLED = (int8_t)0xFF;
    PROC1_$CURRENT = TEST_PID;
    waitn_result = 2;
    fw_calls = 0;
    unbind_calls = 0;

    AUDIT_$SERVER();

    ASSERT_EQ(1, fw_calls);
    ASSERT_EQ(0xAABBCCDDu, fw_uid.high);
    ASSERT_EQ(0, AUDIT_$DATA.dirty);      /* 0x00E711BE: clr.b */
}

/* 0x00E710DC / 0x00E710E2 / 0x00E711E8 / 0x00E711F2. */
TEST(bookkeeping_around_the_loop)
{
    run_server(0, 0, 1);

    ASSERT_EQ(1, AUDIT_$DATA.suspend_count[TEST_PID - 1]);
    ASSERT_EQ(0, AUDIT_$DATA.server_running);   /* cleared on the way out */
    ASSERT_EQ(1, unbind_calls);
    ASSERT_EQ(9, unbind_pid);
    /* Two start/stop pairs a turn (0x00E7111E, 0x00E7115C, 0x00E71192,
     * 0x00E711D2), all on the wired block + 0x0C. */
    ASSERT_EQ(2, excl_start_calls);
    ASSERT_EQ(2, excl_stop_calls);
    ASSERT_TRUE(excl_last_arg == (void *)(wired_area + 0x0C));
}

int main(void)
{
    printf("AUDIT_$SERVER tests\n");

    RUN_TEST(waitn_gets_two_eventcounts);
    RUN_TEST(count_is_one_without_the_timeout_flag);
    RUN_TEST(default_timeout);
    RUN_TEST(configured_timeout);
    RUN_TEST(timeout_wake_flushes_dirty_buffer);
    RUN_TEST(bookkeeping_around_the_loop);

    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
