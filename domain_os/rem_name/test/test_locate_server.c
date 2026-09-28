/*
 * rem_name/test/test_locate_server.c - unit tests for REM_NAME_$LOCATE_SERVER
 * (0x00E4A722).  REM_NAME_SERVER_LOCAL and rem_name_$send_request are mocked.
 */

#include <stdio.h>
#include <string.h>

static int tests_passed = 0;
static int tests_failed = 0;
static int current_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do {                     \
    printf("  %-52s ", #name);                  \
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

#include "rem_name/rem_name_internal.h"

rem_name_data_t rem_name_$data;
uint32_t NODE_$ME;

static boolean server_local;
boolean REM_NAME_SERVER_LOCAL(void) { return server_local; }

/* ---- rem_name_$send_request mock: one script entry per call ---- */
static int      sr_calls;
static boolean  sr_result[2];
static status_$t sr_status[2];
static int16_t  sr_reply_len[2];
static uint32_t sr_net_seen[2], sr_node_seen[2];
static int16_t  sr_flags_seen[2], sr_opcode_seen[2], sr_size_seen[2];
static uint32_t sr_req_opcode_seen[2];
static uint16_t sr_req_one_seen[2];
static uint8_t  reply_node_bytes[4];

boolean rem_name_$send_request(uint32_t net, uint32_t node, void *request,
                               int16_t req_size, int16_t flags, int16_t opcode,
                               void *response, int16_t resp_size,
                               int16_t *resp_len_ret, status_$t *status_ret)
{
    int n = sr_calls++;
    (void)resp_size;
    sr_net_seen[n] = net; sr_node_seen[n] = node;
    sr_flags_seen[n] = flags; sr_opcode_seen[n] = opcode; sr_size_seen[n] = req_size;
    memcpy(&sr_req_opcode_seen[n], request, 4);
    memcpy(&sr_req_one_seen[n], (uint8_t *)request + 0xC, 2);
    memset(response, 0, 0x16a);
    memcpy((uint8_t *)response + 0x1E, reply_node_bytes, 4);
    *resp_len_ret = sr_reply_len[n];
    *status_ret = sr_status[n];
    return sr_result[n];
}

#include "../locate_server.c"

static void reset(void)
{
    memset(&rem_name_$data, 0xEE, sizeof(rem_name_$data));
    NODE_$ME = 0x1234;
    server_local = 0;
    sr_calls = 0;
    sr_result[0] = sr_result[1] = (boolean)-1;
    sr_status[0] = sr_status[1] = status_$ok;
    sr_reply_len[0] = sr_reply_len[1] = 0x28;
    /* big-endian longword 0x12ABCDE5 at reply+0x1E -> node 0xABCDE5 & 0xFFFFF */
    reply_node_bytes[0] = 0x12; reply_node_bytes[1] = 0xAB;
    reply_node_bytes[2] = 0xCD; reply_node_bytes[3] = 0xE5;
}

/* server not local: one broadcast, found */
TEST(broadcast_finds_server)
{
    uint32_t node = 0, net = 0x99;
    status_$t st = 0x55;

    reset();
    REM_NAME_$LOCATE_SERVER(&node, &net, &st);
    ASSERT_EQ(1, sr_calls);
    ASSERT_EQ(0, sr_net_seen[0]);
    ASSERT_EQ(0xFFFFFF, sr_node_seen[0]);
    ASSERT_EQ(0x80, sr_flags_seen[0]);
    ASSERT_EQ(0x1E, sr_opcode_seen[0]);
    ASSERT_EQ(0x32, sr_size_seen[0]);
    ASSERT_EQ(REM_NAME_OP_LOCATE_SERVER, sr_req_opcode_seen[0]);
    ASSERT_EQ(1, sr_req_one_seen[0]);
    ASSERT_EQ(0, net);
    ASSERT_EQ(0xBCDE5, node);                   /* +0x1E, 20 bits */
    ASSERT_EQ(0xBCDE5, rem_name_$data.curr_node);
    ASSERT_EQ(0, rem_name_$data.curr_net);
    ASSERT_EQ(status_$ok, rem_name_$data.last_status);
    ASSERT_EQ(status_$ok, st);
}

/* server local: NODE_$ME first, exactly 0x28 accepted */
TEST(local_server_asked_first)
{
    uint32_t node = 0, net = 0x99;
    status_$t st = 0x55;

    reset();
    server_local = (boolean)-1;
    REM_NAME_$LOCATE_SERVER(&node, &net, &st);
    ASSERT_EQ(1, sr_calls);
    ASSERT_EQ(0x1234, sr_node_seen[0]);
    ASSERT_EQ(0, sr_flags_seen[0]);
    ASSERT_EQ(0x1E, sr_opcode_seen[0]);
    ASSERT_EQ(0xBCDE5, node);
    ASSERT_EQ(status_$ok, st);
}

/* local reply of any other length (0x00E4A78C beq) falls to the broadcast */
TEST(local_wrong_length_then_broadcast)
{
    uint32_t node = 0, net = 0x99;
    status_$t st = 0x55;

    reset();
    server_local = (boolean)-1;
    sr_reply_len[0] = 0x29;
    REM_NAME_$LOCATE_SERVER(&node, &net, &st);
    ASSERT_EQ(2, sr_calls);
    ASSERT_EQ(0xFFFFFF, sr_node_seen[1]);
    ASSERT_EQ(0x80, sr_flags_seen[1]);
    ASSERT_EQ(0xBCDE5, node);

    /* local send failure also falls through */
    reset();
    server_local = (boolean)-1;
    sr_result[0] = 0; sr_status[0] = 0x000E0033;
    REM_NAME_$LOCATE_SERVER(&node, &net, &st);
    ASSERT_EQ(2, sr_calls);
    ASSERT_EQ(status_$ok, st);
}

/* 0x00E4A7C4 bpl -> 0x00E4A7F6: a failed broadcast leaves last_status alone */
TEST(broadcast_failure_keeps_last_status)
{
    uint32_t node = 7, net = 0x99;
    status_$t st = 0x55;

    reset();
    sr_result[0] = 0; sr_status[0] = 0x000E0033;
    REM_NAME_$LOCATE_SERVER(&node, &net, &st);
    ASSERT_EQ(0x000E0033, st);
    ASSERT_EQ(0xEEEEEEEE, (uint32_t)rem_name_$data.last_status);
    ASSERT_EQ(7, node);
    ASSERT_EQ(0x99, net);
}

/* 0x00E4A7CA `cmpi.w #0x28 / bge`: short broadcast reply -> 0xE001C, recorded */
TEST(broadcast_short_reply)
{
    uint32_t node = 7, net = 0x99;
    status_$t st = 0x55;

    reset();
    sr_reply_len[0] = 0x27;
    REM_NAME_$LOCATE_SERVER(&node, &net, &st);
    ASSERT_EQ(status_$naming_helper_sent_packets_with_errors, st);
    ASSERT_EQ(status_$naming_helper_sent_packets_with_errors, rem_name_$data.last_status);
    ASSERT_EQ(7, node);
    ASSERT_EQ(0xEEEEEEEE, rem_name_$data.curr_node);

    /* but a LONGER broadcast reply is fine (bge) */
    reset();
    sr_reply_len[0] = 0x40;
    REM_NAME_$LOCATE_SERVER(&node, &net, &st);
    ASSERT_EQ(0xBCDE5, node);
    ASSERT_EQ(status_$ok, st);
}

int main(void)
{
    printf("REM_NAME_$LOCATE_SERVER tests\n");
    RUN_TEST(broadcast_finds_server);
    RUN_TEST(local_server_asked_first);
    RUN_TEST(local_wrong_length_then_broadcast);
    RUN_TEST(broadcast_failure_keeps_last_status);
    RUN_TEST(broadcast_short_reply);
    printf("%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
