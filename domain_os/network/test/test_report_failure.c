/*
 * network/test/test_report_failure.c - unit tests for
 * NETWORK_$REPORT_FAILURE (0x00E103FA)
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

static int tests_passed = 0;
static int tests_failed = 0;

static void reset_state(void);

#define RUN_TEST(name) do { \
    printf("  Running %s... ", #name); \
    reset_state(); \
    test_##name(); \
    printf("PASSED\n"); \
    tests_passed++; \
} while (0)

#define ASSERT_EQ(expected, actual) do { \
    if ((unsigned long)(expected) != (unsigned long)(actual)) { \
        printf("FAILED\n    Expected: 0x%lx, Got: 0x%lx at line %d\n", \
               (unsigned long)(expected), (unsigned long)(actual), __LINE__); \
        tests_failed++; \
        return; \
    } \
} while (0)

#include "network/network_internal.h"
#include "net_io/net_io.h"
#include "node/node.h"
#include "time/time.h"

network_$failure_rec_t NETWORK_$FAILURE_REC;
pkt_$info_t NETWORK_$SERVER_PKT_INFO = { .flags = 8, .routing_type = 2, .protocol = 0x8031 };
uint16_t NETWORK_$REPORT_SEND_FLAGS = 1;
uint32_t NODE_$ME;
uint32_t TIME_$CLOCKH;

static uint32_t gethdr_node;
static int gethdr_calls, bld_calls, send_calls, rtn_calls;
static uint32_t bld_dest, bld_msg0, bld_msg1;
static uint16_t bld_tlen, bld_dsock, bld_flags, send_flags;
static int32_t bld_or;
static status_$t bld_status;
static uint32_t hdr_va_stub;

void NETWORK_$GETHDR(uint32_t *node_ptr, uint32_t *va_out, uint32_t *ppn_out)
{
    gethdr_calls++;
    gethdr_node = *node_ptr;
    *va_out = hdr_va_stub;
    *ppn_out = 0x4400;
}

void NETWORK_$RTNHDR(uint32_t *va_ptr) { (void)va_ptr; rtn_calls++; }

void PKT_$BLD_INTERNET_HDR(uint32_t routing_key, uint32_t dest_node, uint16_t dest_sock,
                           int32_t src_node_or, uint32_t src_node, uint16_t src_sock,
                           const pkt_$info_t *pkt_info, uint16_t request_id,
                           void *template, uint16_t template_len, uint16_t data_len,
                           int16_t *port_out, pkt_$hdr_t *hdr, uint16_t *len_out,
                           uint16_t *retry_hint, uint16_t *timeout_out,
                           status_$t *status_ret)
{
    (void)routing_key; (void)src_node; (void)src_sock; (void)request_id;
    (void)data_len; (void)hdr; (void)retry_hint; (void)timeout_out;
    bld_calls++;
    bld_dest = dest_node;
    bld_dsock = dest_sock;
    bld_or = src_node_or;
    bld_flags = pkt_info->flags;
    bld_tlen = template_len;
    bld_msg0 = ((uint32_t *)template)[0];
    bld_msg1 = ((uint32_t *)template)[1];
    *port_out = 1;
    *len_out = 0x40;
    *status_ret = bld_status;
}

void NET_IO_$SEND(int16_t port, uint32_t *hdr_ptr, uint32_t hdr_pa,
                  uint16_t hdr_len, uint32_t data_va, uint32_t *data_pages,
                  int16_t data_len, uint16_t flags,
                  net_io_$send_info_t *send_info, status_$t *status_ret)
{
    (void)port; (void)hdr_ptr; (void)hdr_pa; (void)hdr_len; (void)data_va;
    (void)data_pages; (void)data_len; (void)send_info;
    send_calls++;
    send_flags = flags;
    *status_ret = 0;
}

#include "../report_failure.c"

static void reset_state(void)
{
    memset(&NETWORK_$FAILURE_REC, 0, sizeof(NETWORK_$FAILURE_REC));
    NODE_$ME = 0x1234;
    TIME_$CLOCKH = 0x5555;
    gethdr_calls = bld_calls = send_calls = rtn_calls = 0;
    bld_status = 0;
    hdr_va_stub = 0x8000;
}

static void test_bit5_is_type_1(void)
{
    uint16_t w = 0x0020;
    NETWORK_$REPORT_FAILURE(&w);
    ASSERT_EQ(0x5555, NETWORK_$FAILURE_REC.timestamp);
    ASSERT_EQ((uint8_t)0xFF, (uint8_t)NETWORK_$FAILURE_REC.flag);
    ASSERT_EQ(0x1234, NETWORK_$FAILURE_REC.node_id);
    ASSERT_EQ(1, NETWORK_$FAILURE_REC.failure_type);
    ASSERT_EQ(1, gethdr_node);                 /* the type cell as the node */
    ASSERT_EQ(1, bld_dest);
    ASSERT_EQ(4, bld_dsock);
    ASSERT_EQ(-1, bld_or);
    ASSERT_EQ(0x90, bld_flags);
    ASSERT_EQ(0x18, bld_tlen);
    ASSERT_EQ(0x0003000E, bld_msg0);
    ASSERT_EQ(1, bld_msg1);
    ASSERT_EQ(1, send_calls);
    ASSERT_EQ(1, send_flags);
    ASSERT_EQ(1, rtn_calls);
}

static void test_other_is_type_3(void)
{
    uint16_t w = 0x0400;
    NETWORK_$REPORT_FAILURE(&w);
    ASSERT_EQ(3, NETWORK_$FAILURE_REC.failure_type);
    ASSERT_EQ(3, bld_msg1);
}

static void test_build_failure_skips_send(void)
{
    uint16_t w = 0;
    bld_status = 0x00110008;
    NETWORK_$REPORT_FAILURE(&w);
    ASSERT_EQ(0, send_calls);
    ASSERT_EQ(1, rtn_calls);
}

static void test_no_header_no_return(void)
{
    uint16_t w = 0;
    hdr_va_stub = 0;
    NETWORK_$REPORT_FAILURE(&w);
    ASSERT_EQ(0, rtn_calls);
}

int main(void)
{
    printf("NETWORK_$REPORT_FAILURE tests:\n");
    RUN_TEST(bit5_is_type_1);
    RUN_TEST(other_is_type_3);
    RUN_TEST(build_failure_skips_send);
    RUN_TEST(no_header_no_return);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
