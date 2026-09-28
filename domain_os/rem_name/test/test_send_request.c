/*
 * rem_name/test/test_send_request.c - unit tests for rem_name_$send_request
 * (0x00E4A4C8).  PKT_$SAR_INTERNET is mocked; it records every argument and
 * fills the reply from a script.
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

/* ---- PKT_$SAR_INTERNET mock ---- */
static int       pkt_calls;
static uint32_t  pkt_key_seen, pkt_node_seen;
static uint16_t  pkt_sock_seen;
static uint16_t  pkt_info_seen[15];
static int16_t   pkt_timeout_seen;
static void     *pkt_req_seen;
static uint16_t  pkt_req_len_seen;
static const uint8_t *pkt_data_seen;
static uint16_t  pkt_data_len_seen;
static void     *pkt_resp_seen;
static uint16_t  pkt_resp_max_seen;
static uint16_t  pkt_data_max_seen;
static status_$t pkt_status;
static uint16_t  pkt_reply_len;
static uint16_t  pkt_reply_opcode;
static status_$t pkt_reply_status;

void PKT_$SAR_INTERNET(uint32_t routing_key, uint32_t dest_node, uint16_t dest_sock,
                       void *pkt_info, int16_t timeout,
                       void *req_template, uint16_t req_tpl_len,
                       void *req_data, uint16_t req_data_len,
                       pkt_$sar_result_t *resp_buf,
                       char *resp_tpl_buf, uint16_t resp_tpl_max,
                       uint16_t *resp_tpl_len, void *resp_data_buf, uint16_t resp_data_max,
                       uint16_t *resp_data_len, status_$t *status_ret)
{
    rem_name_$reply_hdr_t *hdr = (rem_name_$reply_hdr_t *)resp_tpl_buf;
    (void)resp_buf; (void)resp_data_buf;
    pkt_calls++;
    pkt_key_seen = routing_key; pkt_node_seen = dest_node; pkt_sock_seen = dest_sock;
    memcpy(pkt_info_seen, pkt_info, sizeof(pkt_info_seen));
    pkt_timeout_seen = timeout;
    pkt_req_seen = req_template; pkt_req_len_seen = req_tpl_len;
    pkt_data_seen = req_data; pkt_data_len_seen = req_data_len;
    pkt_resp_seen = resp_tpl_buf; pkt_resp_max_seen = resp_tpl_max;
    pkt_data_max_seen = resp_data_max;
    *resp_tpl_len = pkt_reply_len;
    *resp_data_len = 0;
    hdr->opcode = pkt_reply_opcode;
    hdr->status = pkt_reply_status;
    *status_ret = pkt_status;
}

#include "../send_request.c"

static uint8_t request[0x40];
static uint8_t response[0x16a];
static int16_t resp_len;

static void reset(void)
{
    int i;
    memset(&rem_name_$data, 0, sizeof(rem_name_$data));
    for (i = 0; i < 15; i++) rem_name_$data.config[i] = (uint16_t)(0x1000 + i);
    rem_name_$data.service_delay = 0x10;
    rem_name_$data.last_status = 0x7777;
    pkt_calls = 0;
    pkt_status = status_$ok;
    pkt_reply_len = 0x40;
    pkt_reply_opcode = 0x1E;
    pkt_reply_status = status_$ok;
    memset(response, 0, sizeof(response));
}

TEST(argument_list_and_success)
{
    status_$t st = 0x55;
    boolean r;

    reset();
    r = rem_name_$send_request(0x0A, 0x0B, request, 0x32, 0x80, 0x1E,
                               response, 0x16a, &resp_len, &st);

    ASSERT_EQ(0xFF, (uint8_t)r);
    ASSERT_EQ(status_$ok, st);
    ASSERT_EQ(0x7777, rem_name_$data.last_status);   /* untouched on success */
    ASSERT_EQ(1, pkt_calls);
    ASSERT_EQ(0x0A, pkt_key_seen);
    ASSERT_EQ(0x0B, pkt_node_seen);
    ASSERT_EQ(10, pkt_sock_seen);
    ASSERT_EQ(0x1080, pkt_info_seen[0]);             /* config[0] | flags */
    ASSERT_EQ(0x1001, pkt_info_seen[1]);
    ASSERT_EQ(0x100E, pkt_info_seen[14]);
    ASSERT_EQ(0x1000, rem_name_$data.config[0]);     /* the module copy is untouched */
    ASSERT_EQ(0x10, pkt_timeout_seen);
    ASSERT_EQ((uintptr_t)request, (uintptr_t)pkt_req_seen);
    ASSERT_EQ(0x32, pkt_req_len_seen);
    ASSERT_EQ((uintptr_t)rem_name_$empty_req_data_00e4a584, (uintptr_t)pkt_data_seen);
    ASSERT_EQ(0, pkt_data_len_seen);
    ASSERT_EQ((uintptr_t)response, (uintptr_t)pkt_resp_seen);
    ASSERT_EQ(0x16a, pkt_resp_max_seen);
    ASSERT_EQ(0, pkt_data_max_seen);
    ASSERT_EQ(0x40, resp_len);
}

/* 0x00E4A542-0x00E4A54E */
TEST(transport_failure)
{
    status_$t st = 0x55;

    reset();
    pkt_status = 0x00040005;
    ASSERT_EQ(0, (uint8_t)rem_name_$send_request(0, 0, request, 0x32, 0, 0x1E,
                                                 response, 0x16a, &resp_len, &st));
    ASSERT_EQ(0x00040005, st);
    ASSERT_EQ(0x00040005, rem_name_$data.last_status);
}

/* 0x00E4A550 `cmpi.w #0x12 / blt`: short (and negative) lengths fail with
 * the reply's own status word */
TEST(short_reply_fails_with_reply_status)
{
    status_$t st = 0x55;

    reset();
    pkt_reply_len = 0x11;
    pkt_reply_status = 0x000E0009;
    ASSERT_EQ(0, (uint8_t)rem_name_$send_request(0, 0, request, 0x32, 0, 0x1E,
                                                 response, 0x16a, &resp_len, &st));
    ASSERT_EQ(0x000E0009, st);
    ASSERT_EQ(0x000E0009, rem_name_$data.last_status);

    reset();
    pkt_reply_len = 0x12;                       /* exactly 0x12 is enough */
    ASSERT_EQ(0xFF, (uint8_t)rem_name_$send_request(0, 0, request, 0x32, 0, 0x1E,
                                                    response, 0x16a, &resp_len, &st));
}

/* 0x00E4A556-0x00E4A562: sign-extended opcode vs zero-extended reply word */
TEST(opcode_mismatch)
{
    status_$t st = 0x55;

    reset();
    pkt_reply_opcode = 0x1C;
    pkt_reply_status = 0x000E0010;
    ASSERT_EQ(0, (uint8_t)rem_name_$send_request(0, 0, request, 0x32, 0, 0x1E,
                                                 response, 0x16a, &resp_len, &st));
    ASSERT_EQ(0x000E0010, st);

    /* opcode -1 (0xFFFF sign-extended) never equals the reply's 0xFFFF */
    reset();
    pkt_reply_opcode = 0xFFFF;
    pkt_reply_status = 0x000E0011;
    ASSERT_EQ(0, (uint8_t)rem_name_$send_request(0, 0, request, 0x32, 0, (int16_t)-1,
                                                 response, 0x16a, &resp_len, &st));
    ASSERT_EQ(0x000E0011, st);
}

/* 0x00E4A564: a non-zero status inside an otherwise good reply */
TEST(reply_status_nonzero)
{
    status_$t st = 0x55;

    reset();
    pkt_reply_status = 0x000E0007;
    ASSERT_EQ(0, (uint8_t)rem_name_$send_request(0, 0, request, 0x32, 0, 0x1E,
                                                 response, 0x16a, &resp_len, &st));
    ASSERT_EQ(0x000E0007, st);
    ASSERT_EQ(0x000E0007, rem_name_$data.last_status);
}

int main(void)
{
    printf("rem_name_$send_request tests\n");
    RUN_TEST(argument_list_and_success);
    RUN_TEST(transport_failure);
    RUN_TEST(short_reply_fails_with_reply_status);
    RUN_TEST(opcode_mismatch);
    RUN_TEST(reply_status_nonzero);
    printf("%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
