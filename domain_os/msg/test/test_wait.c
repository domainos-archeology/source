/*
 * msg/test/test_wait.c - Unit tests for MSG_$WAIT / MSG_$WAITI (0x00E59BA4 /
 * 0x00E59BC0).
 *
 * The test compiles the real msg/wait.c and supplies the globals it reaches
 * (the ownership bitmap, the socket table, PROC1_$AS_ID, TIME_$CLOCKH, the FIM
 * quit arrays) plus a scripted EC_$WAIT, so every branch of the original can be
 * driven from C.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ==========================================================================
 * Test framework
 * ========================================================================== */

static int tests_failed = 0;
static int tests_run = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name)                                                        \
    do {                                                                      \
        printf("  Running %s... ", #name);                                    \
        tests_run++;                                                          \
        test_##name();                                                        \
        printf("done\n");                                                     \
    } while (0)

#define ASSERT_EQ(expected, actual)                                           \
    do {                                                                      \
        long long _e = (long long)(expected);                                 \
        long long _a = (long long)(actual);                                   \
        if (_e != _a) {                                                       \
            printf("FAILED\n    Expected: 0x%llx, Got: 0x%llx at line %d\n",  \
                   (unsigned long long)_e, (unsigned long long)_a, __LINE__); \
            tests_failed++;                                                   \
            return;                                                           \
        }                                                                     \
    } while (0)

/* ==========================================================================
 * Globals the code under test links against
 * ========================================================================== */

#include "msg/msg_internal.h"

/*
 * The MSG globals: the ownership bitmaps live in MSG_$UNWIRED_DATA, so
 * defining the block is enough.
 */
MODULE_DATA_DEFINE(msg_$unwired_data_t, MSG_$UNWIRED_DATA, 0x00E80D84);

/* The SOCK module data block; the socket table is SOCK_$DATA.socket_ptr. */
MODULE_DATA_DEFINE(sock_$data_t, SOCK_$DATA, 0x00E27510);

uint16_t PROC1_$AS_ID;
ec_$eventcount_t TIME_$CLOCKH_EC = { .value = (int32_t)(0) };  /* TIME_$CLOCKH = its value */

#include "fim/fim.h"
MODULE_DATA_DEFINE(fim_$wired_data_t, FIM_$WIRED_DATA, 0x00E21FE6);

/* Scripted EC_$WAIT: records what it was handed and returns ec_wait_result. */
static int16_t ec_wait_result;
static int ec_wait_calls;
static ec_$wait_ecs_t ec_wait_ecs;
static ec_$wait_vals_t ec_wait_vals;

int16_t EC_$WAIT(ec_$wait_ecs_t ecs, ec_$wait_vals_t vals)
{
    ec_wait_calls++;
    ec_wait_ecs = ecs;
    ec_wait_vals = vals;
    return ec_wait_result;
}

#include "../wait.c"

/* ==========================================================================
 * Helpers
 * ========================================================================== */

#define TEST_ASID   3
#define TEST_SOCK   5

static sock_$sock_t test_sock;

static void reset_state(void)
{
    memset(&MSG_$UNWIRED_DATA, 0, sizeof(MSG_$UNWIRED_DATA));
    memset(&SOCK_$DATA, 0, sizeof(SOCK_$DATA));
    memset(&test_sock, 0, sizeof(test_sock));
    memset(FIM_$WIRED_DATA.quit_value, 0, sizeof(FIM_$WIRED_DATA.quit_value));
    memset(FIM_$WIRED_DATA.quit_ec, 0, sizeof(FIM_$WIRED_DATA.quit_ec));

    PROC1_$AS_ID = TEST_ASID;
    TIME_$CLOCKH = 0;
    ec_wait_result = 0;
    ec_wait_calls = 0;

    SOCK_$DATA.socket_ptr[TEST_SOCK] = (sock_$sock_t *)&test_sock.ec;
}

/* Grant TEST_ASID ownership of socket "sock" the way MSG_$OPENI would:
 * byte (0x3F - asid) >> 3, bit asid & 7. */
static void grant_ownership(int sock, uint16_t asid)
{
    MSG_$UNWIRED_DATA.ownership[sock][(uint16_t)(0x3F - asid) >> 3] |= (uint8_t)(1 << (asid & 7));
}

/* ==========================================================================
 * Tests
 * ========================================================================== */

/* 0x00E59BD8 / 0x00E59BDA: socket must be 1..0xE0 inclusive. */
TEST(socket_range_check)
{
    msg_$socket_t sock;
    int16_t timeout = 10;
    status_$t status;

    reset_state();

    sock = 0;
    status = 0x5A5A5A5A;
    MSG_$WAITI(&sock, &timeout, &status);
    ASSERT_EQ(status_$msg_socket_out_of_range, status);

    sock = MSG_MAX_SOCKET + 1;   /* 0xE1 */
    status = 0x5A5A5A5A;
    MSG_$WAITI(&sock, &timeout, &status);
    ASSERT_EQ(status_$msg_socket_out_of_range, status);

    /* 0xE0 is in range, so it must get past the range test and fail on
     * ownership instead. */
    sock = MSG_MAX_SOCKET;
    status = 0x5A5A5A5A;
    MSG_$WAITI(&sock, &timeout, &status);
    ASSERT_EQ(status_$msg_no_owner, status);

    ASSERT_EQ(0, ec_wait_calls);
}

/* 0x00E59BF0 - 0x00E59C0C: the ownership bit is byte (0x3F-asid)>>3, bit
 * asid&7 of the socket's own 8-byte bitmap. */
TEST(ownership_bitmap_indexing)
{
    msg_$socket_t sock = TEST_SOCK;
    int16_t timeout = 10;
    status_$t status;

    reset_state();

    status = 0x5A5A5A5A;
    MSG_$WAITI(&sock, &timeout, &status);
    ASSERT_EQ(status_$msg_no_owner, status);

    /* asid 3 -> byte (0x3F-3)>>3 == 7, bit 3 */
    grant_ownership(TEST_SOCK, TEST_ASID);
    ASSERT_EQ(7, (int)((uint16_t)(0x3F - TEST_ASID) >> 3));
    ASSERT_EQ(0x08, MSG_$UNWIRED_DATA.ownership[TEST_SOCK][7]);

    /* A bit set for the same ASID on a *neighbouring* socket must not count. */
    reset_state();
    grant_ownership(TEST_SOCK - 1, TEST_ASID);
    grant_ownership(TEST_SOCK + 1, TEST_ASID);
    status = 0x5A5A5A5A;
    MSG_$WAITI(&sock, &timeout, &status);
    ASSERT_EQ(status_$msg_no_owner, status);

    /* asid 63 lands in byte 0, bit 7 */
    reset_state();
    PROC1_$AS_ID = 63;
    grant_ownership(TEST_SOCK, 63);
    ASSERT_EQ(0x80, MSG_$UNWIRED_DATA.ownership[TEST_SOCK][0]);
    test_sock.queue_count = 1;
    status = 0x5A5A5A5A;
    MSG_$WAITI(&sock, &timeout, &status);
    ASSERT_EQ(status_$ok, status);
}

/* 0x00E59C3C - 0x00E59C44: a non-zero queue_count short-circuits the wait. */
TEST(already_queued_skips_wait)
{
    msg_$socket_t sock = TEST_SOCK;
    int16_t timeout = 10;
    status_$t status = 0x5A5A5A5A;

    reset_state();
    grant_ownership(TEST_SOCK, TEST_ASID);
    test_sock.queue_count = 1;

    MSG_$WAITI(&sock, &timeout, &status);

    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(0, ec_wait_calls);
}

/* 0x00E59C46 - 0x00E59C7A: the three event counts and the three wait values. */
TEST(ec_wait_argument_build)
{
    msg_$socket_t sock = TEST_SOCK;
    int16_t timeout = 0x0040;
    status_$t status = 0x5A5A5A5A;

    reset_state();
    grant_ownership(TEST_SOCK, TEST_ASID);
    test_sock.ec.value = 100;
    FIM_$WIRED_DATA.quit_value[TEST_ASID] = 7;
    TIME_$CLOCKH = 0x1000;

    MSG_$WAITI(&sock, &timeout, &status);

    ASSERT_EQ(1, ec_wait_calls);
    ASSERT_EQ((uintptr_t)&test_sock.ec, (uintptr_t)ec_wait_ecs.ec[0]);
    ASSERT_EQ((uintptr_t)&TIME_$CLOCKH, (uintptr_t)ec_wait_ecs.ec[1]);
    ASSERT_EQ((uintptr_t)&FIM_$WIRED_DATA.quit_ec[TEST_ASID], (uintptr_t)ec_wait_ecs.ec[2]);
    ASSERT_EQ(101, ec_wait_vals.val[0]);
    ASSERT_EQ(0x1040, ec_wait_vals.val[1]);
    ASSERT_EQ(8, ec_wait_vals.val[2]);
    ASSERT_EQ(status_$ok, status);
}

/* 0x00E59C4C: only a *word* is read from the timeout pointer and it is
 * zero-extended before being added to TIME_$CLOCKH. */
TEST(timeout_is_a_zero_extended_word)
{
    msg_$socket_t sock = TEST_SOCK;
    int16_t timeout = (int16_t)0xFFFF;
    status_$t status = 0x5A5A5A5A;

    reset_state();
    grant_ownership(TEST_SOCK, TEST_ASID);
    TIME_$CLOCKH = 1;

    MSG_$WAITI(&sock, &timeout, &status);

    ASSERT_EQ(0x10000, ec_wait_vals.val[1]);
}

/* 0x00E59C86 - 0x00E59C94: index 0 ok, 1 timeout, 2 quit, anything else
 * leaves the status untouched. */
TEST(ec_wait_result_dispatch)
{
    msg_$socket_t sock = TEST_SOCK;
    int16_t timeout = 10;
    status_$t status;

    reset_state();
    grant_ownership(TEST_SOCK, TEST_ASID);

    ec_wait_result = 0;
    status = 0x5A5A5A5A;
    MSG_$WAITI(&sock, &timeout, &status);
    ASSERT_EQ(status_$ok, status);

    ec_wait_result = 1;
    status = 0x5A5A5A5A;
    MSG_$WAITI(&sock, &timeout, &status);
    ASSERT_EQ(status_$msg_time_out, status);

    ec_wait_result = 3;
    status = 0x5A5A5A5A;
    MSG_$WAITI(&sock, &timeout, &status);
    ASSERT_EQ(0x5A5A5A5A, status);
}

/* 0x00E59CA2 - 0x00E59CCA: the quit path copies FIM_$WIRED_DATA.quit_ec[asid].value into
 * FIM_$WIRED_DATA.quit_value[asid] before reporting the quit fault. */
TEST(quit_path_latches_the_eventcount)
{
    msg_$socket_t sock = TEST_SOCK;
    int16_t timeout = 10;
    status_$t status = 0x5A5A5A5A;

    reset_state();
    grant_ownership(TEST_SOCK, TEST_ASID);
    FIM_$WIRED_DATA.quit_value[TEST_ASID] = 4;
    FIM_$WIRED_DATA.quit_ec[TEST_ASID].value = 99;
    ec_wait_result = 2;

    MSG_$WAITI(&sock, &timeout, &status);

    ASSERT_EQ(status_$msg_quit_fault, status);
    ASSERT_EQ(99, FIM_$WIRED_DATA.quit_value[TEST_ASID]);
    /* The value pushed to EC_$WAIT was still the pre-quit one + 1. */
    ASSERT_EQ(5, ec_wait_vals.val[2]);
}

/*
 * 0x00E59BA4: MSG_$WAIT takes only (socket, timeout) and returns the Domain
 * boolean formed by "seq D0b" on its own local status.
 */
TEST(wait_wrapper_returns_a_domain_boolean)
{
    msg_$socket_t sock = TEST_SOCK;
    int16_t timeout = 10;
    boolean r;

    reset_state();
    grant_ownership(TEST_SOCK, TEST_ASID);

    ec_wait_result = 0;
    r = MSG_$WAIT(&sock, &timeout);
    ASSERT_EQ(-1, r);
    ASSERT_EQ(1, r < 0);

    ec_wait_result = 1;         /* timeout -> status != ok */
    r = MSG_$WAIT(&sock, &timeout);
    ASSERT_EQ(0, r);

    /* Out-of-range socket also yields false. */
    sock = 0;
    r = MSG_$WAIT(&sock, &timeout);
    ASSERT_EQ(0, r);
}

int main(void)
{
    printf("MSG_$WAIT / MSG_$WAITI tests\n");

    RUN_TEST(socket_range_check);
    RUN_TEST(ownership_bitmap_indexing);
    RUN_TEST(already_queued_skips_wait);
    RUN_TEST(ec_wait_argument_build);
    RUN_TEST(timeout_is_a_zero_extended_word);
    RUN_TEST(ec_wait_result_dispatch);
    RUN_TEST(quit_path_latches_the_eventcount);
    RUN_TEST(wait_wrapper_returns_a_domain_boolean);

    printf("%d tests, %d failed\n", tests_run, tests_failed);
    return tests_failed != 0;
}
