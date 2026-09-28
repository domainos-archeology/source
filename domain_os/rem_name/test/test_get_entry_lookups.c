/*
 * rem_name/test/test_get_entry_lookups.c - unit tests for
 * REM_NAME_$GET_ENTRY_BY_NAME (0x00E4A588), REM_NAME_$GET_ENTRY_BY_NODE_ID
 * (0x00E4A800), REM_NAME_$GET_ENTRY_BY_UID (0x00E4A8CC) and REM_NAME_$GET_INFO
 * (0x00E4A690).  rem_name_$send_request is mocked: it records the request
 * bytes and fills the reply from a script.
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
uid_t UID_$NIL = { 0, 0 };

/* ---- rem_name_$send_request mock ---- */
static int      sr_calls;
static boolean  sr_result;
static status_$t sr_status;
static uint32_t sr_net_seen, sr_node_seen;
static uint8_t  sr_req_seen[0x60];
static int16_t  sr_req_size_seen;
static int16_t  sr_flags_seen, sr_opcode_seen;
static int16_t  sr_resp_size_seen;
static int16_t  sr_reply_len;
static uint8_t  sr_reply[0x16a];

boolean rem_name_$send_request(uint32_t net, uint32_t node, void *request,
                               int16_t req_size, int16_t flags, int16_t opcode,
                               void *response, int16_t resp_size,
                               int16_t *resp_len_ret, status_$t *status_ret)
{
    sr_calls++;
    sr_net_seen = net; sr_node_seen = node;
    memcpy(sr_req_seen, request, sizeof(sr_req_seen));
    sr_req_size_seen = req_size;
    sr_flags_seen = flags; sr_opcode_seen = opcode;
    sr_resp_size_seen = resp_size;
    memcpy(response, sr_reply, sizeof(sr_reply));
    *resp_len_ret = sr_reply_len;
    *status_ret = sr_status;
    return sr_result;
}

#include "../get_entry_by_name.c"
#include "../get_entry_by_node_id.c"
#include "../get_entry_by_uid.c"
#include "../get_info.c"

static uid_t dir_uid = { 0x11111111, 0x22222222 };

static void reset(void)
{
    sr_calls = 0;
    sr_result = (boolean)-1;
    sr_status = status_$ok;
    sr_reply_len = 0x42;
    memset(sr_reply, 0, sizeof(sr_reply));
    memset(sr_req_seen, 0, sizeof(sr_req_seen));
}

/* fill the reply's entry at +0x12 */
static void set_reply_entry(int16_t type, uint16_t name_len, const char *name)
{
    rem_name_$entry_reply_t *r = (rem_name_$entry_reply_t *)sr_reply;
    int i;
    r->type = type;
    r->name_len = name_len;
    memset(r->name, 'x', 32);
    memcpy(r->name, name, strlen(name));
    for (i = 0; i < 12; i++) r->data[i] = (uint8_t)(0xA0 + i);
}

static uint32_t req_u32(int off)
{
    uint32_t v; memcpy(&v, sr_req_seen + off, 4); return v;
}
static uint16_t req_u16(int off)
{
    uint16_t v; memcpy(&v, sr_req_seen + off, 2); return v;
}

/* ---------------- GET_ENTRY_BY_NAME ---------------- */

TEST(by_name_rejects_long_names_locally)
{
    rem_name_$dir_entry_t e;
    status_$t st = 0;
    reset();
    REM_NAME_$GET_ENTRY_BY_NAME(1, 2, &dir_uid, "x", 33, &e, &st);
    ASSERT_EQ(0, sr_calls);
    ASSERT_EQ(status_$naming_invalid_pathname, st);
}

TEST(by_name_request_and_normal_reply)
{
    rem_name_$dir_entry_t e;
    status_$t st = 0x55;
    reset();
    set_reply_entry(1, 5, "hello");
    memset(&e, 0xEE, sizeof(e));
    REM_NAME_$GET_ENTRY_BY_NAME(0x10, 0x20, &dir_uid, "abcde", 5, &e, &st);

    ASSERT_EQ(1, sr_calls);
    ASSERT_EQ(0x10, sr_net_seen);
    ASSERT_EQ(0x20, sr_node_seen);
    ASSERT_EQ(REM_NAME_OP_GET_ENTRY_BY_NAME, req_u32(0));
    ASSERT_EQ(0x11111111, req_u32(4));
    ASSERT_EQ(0x22222222, req_u32(8));
    ASSERT_EQ(1, req_u16(0xC));
    ASSERT_EQ(5, req_u16(0x32));
    ASSERT_EQ(0, memcmp(sr_req_seen + 0x34, "abcde", 5));
    ASSERT_EQ(0x34 + 5, sr_req_size_seen);
    ASSERT_EQ(0, sr_flags_seen);
    ASSERT_EQ(2, sr_opcode_seen);
    ASSERT_EQ(0x16a, sr_resp_size_seen);

    ASSERT_EQ(1, e.type);
    ASSERT_EQ(5, e.name_len);
    ASSERT_EQ(0, memcmp(e.name, "helloxxxxxxxxxxxxxxxxxxxxxxxxxxx", 32));
    {
        static const uint8_t want[12] = { 0xA0,0xA1,0xA2,0xA3,0xA4,0xA5,0xA6,0xA7,0xA8,0xA9,0xAA,0xAB };
        ASSERT_EQ(0, memcmp(&e.uid, want, 12));     /* uid + extra, byte-wise */
    }
    ASSERT_EQ(status_$ok, st);
}

TEST(by_name_link_and_missing)
{
    rem_name_$dir_entry_t e;
    status_$t st = 0x55;

    reset();
    set_reply_entry(2, 3, "lnk");
    memset(&e, 0xEE, sizeof(e));
    REM_NAME_$GET_ENTRY_BY_NAME(0, 0, &dir_uid, "a", 1, &e, &st);
    ASSERT_EQ(3, e.type);
    ASSERT_EQ(3, e.name_len);
    ASSERT_EQ(0, e.uid.high);
    ASSERT_EQ(0, e.uid.low);
    ASSERT_EQ(0, e.extra);
    ASSERT_EQ(status_$ok, st);

    reset();
    set_reply_entry(7, 3, "odd");
    memset(&e, 0xEE, sizeof(e));
    REM_NAME_$GET_ENTRY_BY_NAME(0, 0, &dir_uid, "a", 1, &e, &st);
    ASSERT_EQ(0, e.type);
    ASSERT_EQ(3, e.name_len);                 /* still copied */
    ASSERT_EQ(0xEEEEEEEE, e.uid.high);        /* untouched */
    ASSERT_EQ(status_$naming_name_not_found, st);
}

TEST(by_name_send_failure_leaves_entry)
{
    rem_name_$dir_entry_t e;
    status_$t st = 0x55;
    reset();
    sr_result = 0; sr_status = 0x000E0033;
    memset(&e, 0xEE, sizeof(e));
    REM_NAME_$GET_ENTRY_BY_NAME(0, 0, &dir_uid, "", 0, &e, &st);
    ASSERT_EQ(0x34, sr_req_size_seen);
    ASSERT_EQ(0xEEEE, (uint16_t)e.type);
    ASSERT_EQ(0x000E0033, st);
}

/* ---------------- GET_ENTRY_BY_NODE_ID ---------------- */

TEST(by_node_id_request_shape)
{
    rem_name_$dir_entry_t e;
    status_$t st = 0x55;
    reset();
    set_reply_entry(1, 4, "node");
    REM_NAME_$GET_ENTRY_BY_NODE_ID(1, 2, &dir_uid, 0x00ABCDEF, &e, &st);
    ASSERT_EQ(REM_NAME_OP_GET_ENTRY_BY_NODE, req_u32(0));
    ASSERT_EQ(1, req_u16(0xC));
    ASSERT_EQ(0x0800, req_u16(0x32));
    ASSERT_EQ(0x1EABCDEF, req_u32(0x34));
    ASSERT_EQ(0x38, sr_req_size_seen);
    ASSERT_EQ(0x18, sr_opcode_seen);
    ASSERT_EQ(1, e.type);
    ASSERT_EQ(0xA0, ((uint8_t *)&e.uid)[0]);
    ASSERT_EQ(0xAB, ((uint8_t *)&e.uid)[11]);

    reset();
    set_reply_entry(2, 4, "link");          /* no link arm here: not found */
    memset(&e, 0xEE, sizeof(e));
    REM_NAME_$GET_ENTRY_BY_NODE_ID(1, 2, &dir_uid, 0, &e, &st);
    ASSERT_EQ(0, e.type);
    ASSERT_EQ(status_$naming_name_not_found, st);
}

/* ---------------- GET_ENTRY_BY_UID ---------------- */

TEST(by_uid_request_shape)
{
    rem_name_$dir_entry_t e;
    uid_t target = { 0x33333333, 0x44444444 };
    status_$t st = 0x55;
    reset();
    set_reply_entry(1, 2, "id");
    REM_NAME_$GET_ENTRY_BY_UID(1, 2, &dir_uid, &target, &e, &st);
    ASSERT_EQ(REM_NAME_OP_GET_ENTRY_BY_UID, req_u32(0));
    ASSERT_EQ(0x33333333, req_u32(0x32));
    ASSERT_EQ(0x44444444, req_u32(0x36));
    ASSERT_EQ(0x3A, sr_req_size_seen);
    ASSERT_EQ(0x1C, sr_opcode_seen);
    ASSERT_EQ(1, e.type);
    ASSERT_EQ(2, e.name_len);
    ASSERT_EQ(0xA8, ((uint8_t *)&e.extra)[0]);
}

/* ---------------- GET_INFO ---------------- */

TEST(get_info_copies_22_bytes_only_for_len_0x28)
{
    uint8_t info[22];
    uid_t uid = { 0x55555555, 0x66666666 };
    status_$t st = 0x55;
    int i;

    reset();
    for (i = 0; i < 22; i++) sr_reply[0x12 + i] = (uint8_t)(0x30 + i);
    sr_reply_len = 0x28;
    memset(info, 0xEE, sizeof(info));
    REM_NAME_$GET_INFO(1, 2, &uid, info, &st);
    ASSERT_EQ(REM_NAME_OP_GET_INFO, req_u32(0));
    ASSERT_EQ(0x55555555, req_u32(4));
    ASSERT_EQ(1, req_u16(0xC));
    ASSERT_EQ(0x32, sr_req_size_seen);
    ASSERT_EQ(0x1A, sr_opcode_seen);
    for (i = 0; i < 22; i++) ASSERT_EQ(0x30 + i, info[i]);
    ASSERT_EQ(status_$ok, st);

    /* 0x00E4A6F6 `cmpi.w #0x28 / bne`: longer replies are errors too */
    reset();
    sr_reply_len = 0x29;
    memset(info, 0xEE, sizeof(info));
    REM_NAME_$GET_INFO(1, 2, &uid, info, &st);
    ASSERT_EQ(0xEE, info[0]);
    ASSERT_EQ(status_$naming_helper_sent_packets_with_errors, st);

    /* send failure: nothing */
    reset();
    sr_result = 0; sr_status = 0x000E0033; sr_reply_len = 0x28;
    REM_NAME_$GET_INFO(1, 2, &uid, info, &st);
    ASSERT_EQ(0xEE, info[0]);
    ASSERT_EQ(0x000E0033, st);
}

int main(void)
{
    printf("REM_NAME_$GET_ENTRY_BY_* / GET_INFO tests\n");
    RUN_TEST(by_name_rejects_long_names_locally);
    RUN_TEST(by_name_request_and_normal_reply);
    RUN_TEST(by_name_link_and_missing);
    RUN_TEST(by_name_send_failure_leaves_entry);
    RUN_TEST(by_node_id_request_shape);
    RUN_TEST(by_uid_request_shape);
    RUN_TEST(get_info_copies_22_bytes_only_for_len_0x28);
    printf("%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
