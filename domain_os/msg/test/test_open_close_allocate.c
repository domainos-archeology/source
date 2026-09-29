/*
 * msg/test/test_open_close_allocate.c - Unit tests for MSG_$OPEN / MSG_$OPENI
 * (0x00E59198 / 0x00E591B4), MSG_$ALLOCATE / MSG_$ALLOCATEI (0x00E592CA /
 * 0x00E592E6) and MSG_$CLOSE / MSG_$CLOSEI (0x00E593D2 / 0x00E593E4).
 *
 * The real msg/msg_data.c record is used; the SOCK, ML exclusion, PROC2
 * cleanup and NETWORK service callees are mocked and record their calls.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ==========================================================================
 * Test framework
 * ========================================================================== */

static int tests_failed = 0;
static int tests_passed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    int _before = tests_failed; \
    printf("  Running %s... ", #name); \
    fflush(stdout); \
    test_##name(); \
    if (tests_failed == _before) { \
        tests_passed++; \
        printf("PASSED\n"); \
    } \
} while (0)

#define ASSERT_EQ(a, b) do { \
    unsigned long _a = (unsigned long)(a); \
    unsigned long _b = (unsigned long)(b); \
    if (_a != _b) { \
        printf("FAILED\n    %s = 0x%lx (%lu), %s = 0x%lx (%lu) at line %d\n", \
               #a, _a, _a, #b, _b, _b, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while (0)

#define ASSERT_TRUE(cond) do { \
    if (!(cond)) { \
        printf("FAILED\n    %s is false at line %d\n", #cond, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while (0)

/* ==========================================================================
 * Globals and mocked callees
 * ========================================================================== */

#include "msg/msg_internal.h"

uint16_t PROC1_$AS_ID;
int8_t NETWORK_$USER_SOCK_OPEN;

static int excl_start_calls, excl_stop_calls, excl_depth;
static ml_$exclusion_t *excl_last;

void ML_$EXCLUSION_START(ml_$exclusion_t *excl)
{
    excl_start_calls++;
    excl_depth++;
    excl_last = excl;
}

void ML_$EXCLUSION_STOP(ml_$exclusion_t *excl)
{
    excl_stop_calls++;
    excl_depth--;
    excl_last = excl;
}

static int8_t alloc_user_result;
static uint16_t alloc_user_socket;
static int alloc_user_calls;
static uint16_t alloc_user_args[4];

int8_t SOCK_$ALLOCATE_USER(uint16_t *sock_ret, uint16_t proto_hi, uint16_t proto_lo,
                           uint16_t queue_hi, uint16_t queue_lo)
{
    alloc_user_calls++;
    alloc_user_args[0] = proto_hi;
    alloc_user_args[1] = proto_lo;
    alloc_user_args[2] = queue_hi;
    alloc_user_args[3] = queue_lo;
    *sock_ret = alloc_user_socket;
    return alloc_user_result;
}

static int8_t sock_open_result;
static int sock_open_calls;
static uint16_t sock_open_num;
static uint32_t sock_open_bufpages, sock_open_max_queue;

int8_t SOCK_$OPEN(uint16_t sock_num, uint32_t proto_bufpages, uint32_t max_queue)
{
    sock_open_calls++;
    sock_open_num = sock_num;
    sock_open_bufpages = proto_bufpages;
    sock_open_max_queue = max_queue;
    return sock_open_result;
}

static int sock_close_calls;
static uint16_t sock_close_num;

void SOCK_$CLOSE(uint16_t sock_num)
{
    sock_close_calls++;
    sock_close_num = sock_num;
}

static int set_cleanup_calls;
static uint16_t set_cleanup_bit;

void PROC2_$SET_CLEANUP(uint16_t bit_num)
{
    set_cleanup_calls++;
    set_cleanup_bit = bit_num;
}

static int set_service_calls;
static int16_t set_service_op;
static uint32_t set_service_value;

void NETWORK_$SET_SERVICE(int16_t *op_ptr, uint32_t *value_ptr, status_$t *status_p)
{
    set_service_calls++;
    set_service_op = *op_ptr;
    set_service_value = *value_ptr;
    *status_p = status_$ok;
}

/* ==========================================================================
 * Code under test
 * ========================================================================== */

#include "../msg_data.c"
#include "../open.c"
#include "../allocate.c"
#include "../close.c"

/* ==========================================================================
 * Helpers
 * ========================================================================== */

static void reset(void)
{
    memset(&MSG_$UNWIRED_DATA, 0, sizeof(MSG_$UNWIRED_DATA));
    PROC1_$AS_ID = 5;
    NETWORK_$USER_SOCK_OPEN = 0;
    excl_start_calls = excl_stop_calls = excl_depth = 0;
    excl_last = NULL;
    alloc_user_result = (int8_t)0xFF;
    alloc_user_socket = 0x30;
    alloc_user_calls = 0;
    sock_open_result = (int8_t)0xFF;
    sock_open_calls = 0;
    sock_close_calls = 0;
    set_cleanup_calls = 0;
    set_service_calls = 0;
}

/* asid 5: byte (0x3F - 5) >> 3 = 7, bit 5 */
#define ASID5_BYTE 7
#define ASID5_BIT  0x20

/* ==========================================================================
 * MSG_$OPENI / MSG_$OPEN
 * ========================================================================== */

TEST(openi_opens_and_records_ownership)
{
    msg_$socket_t sock = 0x10;
    int16_t depth = 4;
    status_$t status = -1;
    reset();

    MSG_$OPENI(&sock, &depth, &status);
    ASSERT_EQ(status, status_$ok);
    ASSERT_EQ(sock_open_calls, 1);
    ASSERT_EQ(sock_open_num, 0x10);
    ASSERT_EQ(sock_open_bufpages, 0x00040004);
    ASSERT_EQ(sock_open_max_queue, 0x00040400);
    ASSERT_EQ(MSG_$UNWIRED_DATA.ownership[0x10][ASID5_BYTE], ASID5_BIT);
    ASSERT_EQ(MSG_$UNWIRED_DATA.ownership[0x10][0], 0);
    ASSERT_EQ(MSG_$UNWIRED_DATA.depth[0x10], 4);
    ASSERT_EQ(MSG_$UNWIRED_DATA.open_count, 1);
    ASSERT_EQ(set_cleanup_calls, 1);
    ASSERT_EQ(set_cleanup_bit, 7);
    ASSERT_EQ(set_service_calls, 1);
    ASSERT_EQ(set_service_op, 0);
    ASSERT_EQ(set_service_value, 0x80000);
    ASSERT_EQ((uint8_t)NETWORK_$USER_SOCK_OPEN, 0xFF);
    ASSERT_EQ(excl_start_calls, 1);
    ASSERT_EQ(excl_stop_calls, 1);
    ASSERT_EQ(excl_depth, 0);
    ASSERT_TRUE(excl_last == &MSG_$WIRED_DATA.sock_lock);
}

TEST(openi_rejects_range_and_depth_without_locking)
{
    msg_$socket_t sock;
    int16_t depth;
    status_$t status;
    reset();

    sock = 0; depth = 1; status = -1;
    MSG_$OPENI(&sock, &depth, &status);
    ASSERT_EQ(status, status_$msg_socket_out_of_range);

    sock = 0xE0;            /* rejected by MSG_$OPENI (blt), accepted by CLOSEI */
    MSG_$OPENI(&sock, &depth, &status);
    ASSERT_EQ(status, status_$msg_socket_out_of_range);

    sock = 0xDF; depth = 0x21;
    MSG_$OPENI(&sock, &depth, &status);
    ASSERT_EQ(status, status_$msg_too_deep);

    ASSERT_EQ(excl_start_calls, 0);
    ASSERT_EQ(sock_open_calls, 0);
}

TEST(openi_in_use_when_owned_or_sock_open_fails)
{
    msg_$socket_t sock = 0x20;
    int16_t depth = 2;
    status_$t status = -1;
    reset();

    MSG_$UNWIRED_DATA.ownership[0x20][3] = 0x01;
    MSG_$OPENI(&sock, &depth, &status);
    ASSERT_EQ(status, status_$msg_socket_in_use);
    ASSERT_EQ(sock_open_calls, 0);
    ASSERT_EQ(excl_depth, 0);

    MSG_$UNWIRED_DATA.ownership[0x20][3] = 0;
    sock_open_result = 0;
    MSG_$OPENI(&sock, &depth, &status);
    ASSERT_EQ(status, status_$msg_socket_in_use);
    ASSERT_EQ(sock_open_calls, 1);
    ASSERT_EQ(MSG_$UNWIRED_DATA.open_count, 0);
    ASSERT_EQ(excl_depth, 0);
}

TEST(open_returns_boolean_of_the_local_status)
{
    msg_$socket_t sock = 0x10;
    int16_t depth = 4;
    reset();
    ASSERT_EQ((uint8_t)MSG_$OPEN(&sock, &depth), 0xFF);
    sock_open_result = 0;
    sock = 0x11;
    ASSERT_EQ((uint8_t)MSG_$OPEN(&sock, &depth), 0);
}

/* ==========================================================================
 * MSG_$ALLOCATEI / MSG_$ALLOCATE
 * ========================================================================== */

TEST(allocatei_takes_the_socket_sock_allocate_user_returns)
{
    msg_$socket_t sock = 0;
    int16_t depth = 3;
    status_$t status = -1;
    reset();
    alloc_user_socket = 0x33;

    MSG_$ALLOCATEI(&sock, &depth, &status);
    ASSERT_EQ(status, status_$ok);
    ASSERT_EQ(sock, 0x33);
    ASSERT_EQ(alloc_user_calls, 1);
    ASSERT_EQ(alloc_user_args[0], 3);
    ASSERT_EQ(alloc_user_args[1], 3);
    ASSERT_EQ(alloc_user_args[2], 3);
    ASSERT_EQ(alloc_user_args[3], 0x400);
    ASSERT_EQ(MSG_$UNWIRED_DATA.ownership[0x33][ASID5_BYTE], ASID5_BIT);
    ASSERT_EQ(MSG_$UNWIRED_DATA.depth[0x33], 3);
    ASSERT_EQ(MSG_$UNWIRED_DATA.open_count, 1);
    ASSERT_EQ(set_cleanup_bit, 7);
    ASSERT_EQ(set_service_op, 0);
    ASSERT_EQ(set_service_value, 0x80000);
    ASSERT_EQ((uint8_t)NETWORK_$USER_SOCK_OPEN, 0xFF);
    ASSERT_EQ(excl_depth, 0);
}

TEST(allocatei_too_deep_and_no_sockets)
{
    msg_$socket_t sock = 0;
    int16_t depth = 0x21;
    status_$t status = -1;
    reset();

    MSG_$ALLOCATEI(&sock, &depth, &status);
    ASSERT_EQ(status, status_$msg_too_deep);
    ASSERT_EQ(excl_start_calls, 0);

    depth = 0x20;
    alloc_user_result = 0;
    MSG_$ALLOCATEI(&sock, &depth, &status);
    ASSERT_EQ(status, status_$msg_no_more_sockets);
    ASSERT_EQ(excl_start_calls, 1);
    ASSERT_EQ(excl_stop_calls, 1);
    ASSERT_EQ(MSG_$UNWIRED_DATA.open_count, 0);
}

TEST(allocate_returns_boolean_of_the_local_status)
{
    msg_$socket_t sock = 0;
    int16_t depth = 1;
    reset();
    ASSERT_EQ((uint8_t)MSG_$ALLOCATE(&sock, &depth), 0xFF);
    alloc_user_result = 0;
    ASSERT_EQ((uint8_t)MSG_$ALLOCATE(&sock, &depth), 0);
}

/* ==========================================================================
 * MSG_$CLOSEI / MSG_$CLOSE
 * ========================================================================== */

TEST(closei_last_owner_closes_and_drops_the_service)
{
    msg_$socket_t sock = 0x10;
    int16_t depth = 4;
    status_$t status = -1;
    reset();
    MSG_$OPENI(&sock, &depth, &status);
    set_service_calls = 0;
    excl_start_calls = excl_stop_calls = 0;

    MSG_$CLOSEI(&sock, &status);
    ASSERT_EQ(status, status_$ok);
    ASSERT_EQ(MSG_$UNWIRED_DATA.ownership[0x10][ASID5_BYTE], 0);
    ASSERT_EQ(MSG_$UNWIRED_DATA.open_count, 0);
    ASSERT_EQ(sock_close_calls, 1);
    ASSERT_EQ(sock_close_num, 0x10);
    ASSERT_EQ(NETWORK_$USER_SOCK_OPEN, 0);
    ASSERT_EQ(set_service_calls, 1);
    ASSERT_EQ(set_service_op, 1);
    ASSERT_EQ(set_service_value, 0x80000);
    ASSERT_EQ(excl_start_calls, 1);
    ASSERT_EQ(excl_stop_calls, 1);
    ASSERT_EQ(excl_depth, 0);
}

TEST(closei_keeps_the_service_while_other_sockets_are_open)
{
    msg_$socket_t sock = 0x10;
    int16_t depth = 4;
    status_$t status = -1;
    reset();
    MSG_$OPENI(&sock, &depth, &status);
    MSG_$UNWIRED_DATA.open_count = 2;
    set_service_calls = 0;

    MSG_$CLOSEI(&sock, &status);
    ASSERT_EQ(status, status_$ok);
    ASSERT_EQ(MSG_$UNWIRED_DATA.open_count, 1);
    ASSERT_EQ(sock_close_calls, 1);
    ASSERT_EQ((uint8_t)NETWORK_$USER_SOCK_OPEN, 0xFF);
    ASSERT_EQ(set_service_calls, 0);
}

TEST(closei_other_owners_keep_the_socket)
{
    msg_$socket_t sock = 0x10;
    int16_t depth = 4;
    status_$t status = -1;
    reset();
    MSG_$OPENI(&sock, &depth, &status);
    MSG_$UNWIRED_DATA.ownership[0x10][2] = 0x81;    /* two other address spaces */

    MSG_$CLOSEI(&sock, &status);
    ASSERT_EQ(status, status_$ok);
    ASSERT_EQ(MSG_$UNWIRED_DATA.ownership[0x10][ASID5_BYTE], 0);
    ASSERT_EQ(MSG_$UNWIRED_DATA.ownership[0x10][2], 0x81);
    ASSERT_EQ(MSG_$UNWIRED_DATA.open_count, 1);
    ASSERT_EQ(sock_close_calls, 0);
}

TEST(closei_not_owner_and_out_of_range)
{
    msg_$socket_t sock;
    status_$t status;
    reset();

    sock = 0x10; status = -1;
    MSG_$UNWIRED_DATA.ownership[0x10][2] = 0x81;
    MSG_$CLOSEI(&sock, &status);
    ASSERT_EQ(status, status_$msg_no_owner);
    ASSERT_EQ(excl_start_calls, 1);
    ASSERT_EQ(excl_stop_calls, 1);
    ASSERT_EQ(MSG_$UNWIRED_DATA.ownership[0x10][2], 0x81);

    sock = 0;
    MSG_$CLOSEI(&sock, &status);
    ASSERT_EQ(status, status_$msg_socket_out_of_range);
    sock = 0xE1;
    MSG_$CLOSEI(&sock, &status);
    ASSERT_EQ(status, status_$msg_socket_out_of_range);
    ASSERT_EQ(excl_start_calls, 1);

    /* 0xE0 is in range for close */
    sock = 0xE0; status = -1;
    MSG_$CLOSEI(&sock, &status);
    ASSERT_EQ(status, status_$msg_no_owner);
    ASSERT_EQ(excl_start_calls, 2);
}

TEST(close_takes_one_argument_and_reports_nothing)
{
    msg_$socket_t sock = 0x10;
    int16_t depth = 4;
    status_$t status;
    reset();
    MSG_$OPENI(&sock, &depth, &status);
    MSG_$CLOSE(&sock);
    ASSERT_EQ(MSG_$UNWIRED_DATA.open_count, 0);
    ASSERT_EQ(sock_close_calls, 1);
    ASSERT_EQ(excl_depth, 0);
}

/* ==========================================================================
 * main
 * ========================================================================== */

int main(void)
{
    printf("MSG_$OPEN / MSG_$ALLOCATE / MSG_$CLOSE tests\n");

    RUN_TEST(openi_opens_and_records_ownership);
    RUN_TEST(openi_rejects_range_and_depth_without_locking);
    RUN_TEST(openi_in_use_when_owned_or_sock_open_fails);
    RUN_TEST(open_returns_boolean_of_the_local_status);
    RUN_TEST(allocatei_takes_the_socket_sock_allocate_user_returns);
    RUN_TEST(allocatei_too_deep_and_no_sockets);
    RUN_TEST(allocate_returns_boolean_of_the_local_status);
    RUN_TEST(closei_last_owner_closes_and_drops_the_service);
    RUN_TEST(closei_keeps_the_service_while_other_sockets_are_open);
    RUN_TEST(closei_other_owners_keep_the_socket);
    RUN_TEST(closei_not_owner_and_out_of_range);
    RUN_TEST(close_takes_one_argument_and_reports_nothing);

    printf("\nResults: %d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
