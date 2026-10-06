/*
 * network/test/test_write.c - NETWORK_$WRITE (0x00E0FAE6)
 *
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

#include "network/network_internal.h"

char NETWORK_$DO_CHKSUM;
uint16_t NETWORK_$WRITE_CALL_CNT;

static uint16_t pkt_size_ret = 0x800;
uint16_t NETWORK_$GET_PKT_SIZE(uint32_t *dest, uint16_t max)
{ (void)dest; (void)max; return pkt_size_ret; }

static int nchk;
static uint32_t chk_pa_seen;
uint32_t network_$page_chksum(uint32_t *b) { nchk++; chk_pa_seen = *b; return 0xC5C5; }

#define MAXR 8
static int nreq;
static network_$pagout_rqst_t reqs[MAXR];
static uint32_t pa_seen[MAXR];
static uint16_t len_seen[MAXR];
static status_$t req_status;
static network_$pagout_reply_t canned;
void network_$do_request(void *net_handle, void *cmd_buf, int16_t cmd_len,
                         uint32_t param4, uint16_t param5, int16_t check,
                         void *resp_buf, void *resp_info, status_$t *status_ret)
{
    (void)net_handle; (void)resp_info; (void)check;
    if (cmd_len != 0x2A) { tests_failed++; }
    memcpy(&reqs[nreq], cmd_buf, sizeof(reqs[0]));
    pa_seen[nreq] = param4;
    len_seen[nreq] = param5;
    nreq++;
    memcpy(resp_buf, &canned, sizeof(canned));
    canned.version = 0;                 /* later replies must not matter */
    *status_ret = req_status;
}

#include "../write.c"

static uint32_t node[2] = { 0, 0x456 };
static network_$page_request_t req;
static uint16_t dtv;
static clock_t dtm;

static void setup(int16_t version)
{
    memset(&canned, 0, sizeof(canned));
    canned.version = version;
    canned.dtm_low = 0x1234;
    canned.clock.high = 0xAAAA0000;
    canned.clock.low = 0xBBBB;
    canned.u.f.dtm_high = 0xCCCC0000;
    canned.u.f.dtm_bits = 0xFF;
    nreq = nchk = 0;
    NETWORK_$WRITE_CALL_CNT = 0;
    NETWORK_$DO_CHKSUM = 0;
    req_status = 0;
    memset(&req, 0, sizeof(req));
    req.page_num = 0x10;
    dtv = 0; dtm.high = 0; dtm.low = 0;
}

TEST(three_pages_in_two_chunks_version_8)
{
    status_$t st = 1;
    setup(8);
    pkt_size_ret = 0x800;
    NETWORK_$WRITE(node, &req, 0x40, 0x800, 3, &dtv, &dtm, &st);
    ASSERT_EQ(0, st);
    ASSERT_EQ(2, nreq);
    ASSERT_EQ(2, NETWORK_$WRITE_CALL_CNT);
    ASSERT_EQ(4, reqs[0].type);
    ASSERT_EQ(8, reqs[0].version);
    ASSERT_EQ(0xFF, (uint8_t)reqs[0].flag);
    ASSERT_EQ(0xFF, (uint8_t)reqs[1].flag);     /* never cleared */
    ASSERT_EQ(0x10, reqs[0].req.page_num);
    ASSERT_EQ(0x12, reqs[1].req.page_num);
    ASSERT_EQ(0x10000, pa_seen[0]);
    ASSERT_EQ(0x10800, pa_seen[1]);
    ASSERT_EQ(0x800, len_seen[1]);
    ASSERT_EQ(0, reqs[0].chksum);
    ASSERT_EQ(0x1234, dtv);
    ASSERT_EQ(0xAAAA0000, dtm.high);
    ASSERT_EQ(0xBBBB, dtm.low);
    ASSERT_EQ(0x10, req.page_num);              /* caller's record untouched */
}

TEST(version_4_and_old_reply_shapes)
{
    status_$t st = 1;
    setup(4);
    pkt_size_ret = 0x400;
    NETWORK_$WRITE(node, &req, 1, 0x400, 1, &dtv, &dtm, &st);
    ASSERT_EQ(0x1234, dtv);
    ASSERT_EQ(0xCCCC0000, dtm.high);
    ASSERT_EQ(0x1234, dtm.low);
    setup(3);
    NETWORK_$WRITE(node, &req, 1, 0x400, 1, &dtv, &dtm, &st);
    ASSERT_EQ(0xF800, dtv);
    ASSERT_EQ(0xCCCC0000, dtm.high);
    ASSERT_EQ(0xF800, dtm.low);
}

TEST(checksum_and_failure)
{
    status_$t st = 0;
    setup(8);
    pkt_size_ret = 0x400;
    NETWORK_$DO_CHKSUM = -1;
    req_status = 0x110011;
    NETWORK_$WRITE(node, &req, 2, 0x400, 4, &dtv, &dtm, &st);
    ASSERT_EQ(0x110011, st);
    ASSERT_EQ(1, nreq);
    ASSERT_EQ(1, nchk);
    ASSERT_EQ(0x800, chk_pa_seen);
    ASSERT_EQ(0xC5C5, reqs[0].chksum);
    ASSERT_EQ(0, dtv);                          /* not reached */
}

int main(void)
{
    printf("NETWORK_$WRITE\n");
    RUN_TEST(three_pages_in_two_chunks_version_8);
    RUN_TEST(version_4_and_old_reply_shapes);
    RUN_TEST(checksum_and_failure);
    printf("%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
