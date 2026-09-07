/*
 * network/test/test_ring_info.c
 *
 * Layout and behaviour tests for NETWORK_$RING_INFO (0x00E1039A) and the
 * 122-byte ring_info_t it copies out of the reply packet.
 *
 * The layout expectations below are the offsets the responder stores at,
 * NETWORK_$PROCESS_PAGING_REQUEST case 0x0E (0x00E11246..0x00E112D4); the record
 * base there is A0-0x1E2, so an instruction storing at (-0x1E2+N,A0) writes
 * ring_info_t offset N.
 *
 * This test #includes the real network/ring_info.c and stubs only
 * network_$do_request, so the copy loop under test is the shipped one.
 */

#include <stdio.h>
#include <string.h>

/* ============================================================================
 * Test framework
 * ============================================================================ */

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    printf("  Running %s... ", #name); \
    test_##name(); \
    tests_passed++; \
    printf("PASSED\n"); \
} while(0)

#define ASSERT_EQ(expected, actual) do { \
    if ((expected) != (actual)) { \
        printf("FAILED\n    Expected: 0x%lx, Got: 0x%lx at line %d\n", \
               (unsigned long)(expected), (unsigned long)(actual), __LINE__); \
        tests_failed++; \
        return; \
    } \
} while(0)

/* ============================================================================
 * Stub for the one external the code under test calls
 * ============================================================================ */

#include "network/network_internal.h"

/* What the last network_$do_request call was handed. */
static void       *last_handle;
static uint16_t    last_cmd_word;
static int16_t     last_cmd_len;
static status_$t   stub_status;
static uint8_t     stub_response[0x100];

void network_$do_request(void *net_handle, void *cmd_buf, int16_t cmd_len,
                         uint32_t param4, uint16_t param5, int16_t check_flag,
                         void *resp_buf, void *resp_info, status_$t *status_ret)
{
    (void)param4; (void)param5; (void)check_flag; (void)resp_info;
    last_handle = net_handle;
    last_cmd_word = *(uint16_t *)cmd_buf;
    last_cmd_len = cmd_len;
    memcpy(resp_buf, stub_response, sizeof(stub_response));
    *status_ret = stub_status;
}

#include "../ring_info.c"

/* ============================================================================
 * Layout tests - offsets are the responder's stores, case 0x0E
 * ============================================================================ */

TEST(record_is_122_bytes) {
    /* 30 longs + 1 word, 0x00E103E6..0x00E103EE, and the responder fills
     * exactly reply+6..reply+0x7F. */
    ASSERT_EQ(122u, sizeof(ring_info_t));
    ASSERT_EQ(0x7Au, sizeof(ring_info_t));
}

TEST(header_offsets) {
    ASSERT_EQ(0x00u, offsetof(ring_info_t, _unknown_00)); /* move.w #0x3   0x00E112BC */
    ASSERT_EQ(0x02u, offsetof(ring_info_t, diskless));    /* (0x350,A5)    0x00E112C2 */
    ASSERT_EQ(0x04u, offsetof(ring_info_t, mother_node)); /* (0x310,A5)    0x00E112C8 */
    ASSERT_EQ(1u, sizeof(((ring_info_t *)0)->diskless));
    ASSERT_EQ(4u, sizeof(((ring_info_t *)0)->mother_node));
}

TEST(stats_block_is_ring_stats_t) {
    /* 15 longwords from 0x00E261E0 = RING_$STATS[0], 0x00E11266..0x00E11274 */
    ASSERT_EQ(0x08u, offsetof(ring_info_t, stats));
    ASSERT_EQ(0x3Cu, sizeof(((ring_info_t *)0)->stats));
    /* stats+0x02 / +0x06 are the two transmit longs */
    ASSERT_EQ(0x0Au, offsetof(ring_info_t, stats.xmit_call));
    ASSERT_EQ(0x0Eu, offsetof(ring_info_t, stats.xmitcnt));
    ASSERT_EQ(0x22u, offsetof(ring_info_t, stats.xmit_tim));   /* stats+0x1A */
    ASSERT_EQ(0x24u, offsetof(ring_info_t, stats.rcvcnt));     /* stats+0x1C */
    ASSERT_EQ(0x28u, offsetof(ring_info_t, stats.rcveor));     /* stats+0x20 */
    ASSERT_EQ(0x3Au, offsetof(ring_info_t, stats.rcvhcsum));   /* stats+0x32 */
    ASSERT_EQ(0x42u, offsetof(ring_info_t, stats.retry_pending)); /* stats+0x3A */
}

TEST(failure_rec_block) {
    /* four longs from A5+0x2F8 = NETWORK_$FAILURE_REC, 0x00E11256..0x00E11264 */
    ASSERT_EQ(0x44u, offsetof(ring_info_t, failure_rec));
    ASSERT_EQ(0x10u, sizeof(network_$failure_rec_t));
}

TEST(swdiag_block) {
    /* 7 longs + 1 word from 0x00E261C2, 0x00E11278..0x00E1128A, then two
     * longwords patched in over it. */
    ASSERT_EQ(0x54u, offsetof(ring_info_t, swdiag));
    ASSERT_EQ(0x1Eu, sizeof(((ring_info_t *)0)->swdiag));
    ASSERT_EQ(0x56u, offsetof(ring_info_t, swdiag.rcvcnt));   /* 0x00E1128C */
    ASSERT_EQ(0x5Au, offsetof(ring_info_t, swdiag.rcveor));   /* swdiag+0x06 */
    ASSERT_EQ(0x6Au, offsetof(ring_info_t, swdiag.rcvxerr));  /* swdiag+0x16 */
    ASSERT_EQ(0x6Cu, offsetof(ring_info_t, swdiag.rcvhcsum)); /* swdiag+0x18 */
    ASSERT_EQ(0x6Eu, offsetof(ring_info_t, swdiag.nodeid));   /* 0x00E11294 */
    /*
     * Every swdiag mirror sits a uniform 0x1A below its ring_$stats_t
     * counter.  Compare block-relative offsets: the stats block starts at
     * record+0x08 and the swdiag block at record+0x54.
     */
#define STATS_REL(f)  (offsetof(ring_info_t, stats.f)  - 0x08u)
#define SWDIAG_REL(f) (offsetof(ring_info_t, swdiag.f) - 0x54u)
    ASSERT_EQ(0x1Au, STATS_REL(rcveor)   - SWDIAG_REL(rcveor));
    ASSERT_EQ(0x1Au, STATS_REL(rcvxerr)  - SWDIAG_REL(rcvxerr));
    ASSERT_EQ(0x1Au, STATS_REL(rcvhcsum) - SWDIAG_REL(rcvhcsum));
    ASSERT_EQ(0x1Au, STATS_REL(rcvcnt)   - SWDIAG_REL(rcvcnt));
#undef STATS_REL
#undef SWDIAG_REL
}

TEST(tail_biphase_esb_words) {
    ASSERT_EQ(0x72u, offsetof(ring_info_t, xmit_biphase));  /* 0x00E1129C */
    ASSERT_EQ(0x74u, offsetof(ring_info_t, rcv_biphase));   /* 0x00E112A4 */
    ASSERT_EQ(0x76u, offsetof(ring_info_t, xmit_esb));      /* 0x00E112AC */
    ASSERT_EQ(0x78u, offsetof(ring_info_t, rcv_esb));       /* 0x00E112B4 */
}

/* ============================================================================
 * Behaviour tests for NETWORK_$RING_INFO itself
 * ============================================================================ */

TEST(sends_command_0x0e) {
    ring_info_t info;
    status_$t st = 0x5A5A5A5A;
    uint32_t handle = 0x11223344;

    memset(stub_response, 0, sizeof(stub_response));
    stub_status = status_$ok;
    memset(&info, 0, sizeof(info));

    NETWORK_$RING_INFO(&handle, &info, &st);

    ASSERT_EQ((unsigned long)(uintptr_t)&handle, (unsigned long)(uintptr_t)last_handle);
    ASSERT_EQ(0x0E, last_cmd_word);
    ASSERT_EQ(2, last_cmd_len);
    ASSERT_EQ(status_$ok, st);
}

TEST(copies_122_bytes_from_response_offset_6) {
    ring_info_t info;
    status_$t st = 0;
    uint32_t handle = 0;
    unsigned i;

    for (i = 0; i < sizeof(stub_response); i++) {
        stub_response[i] = (uint8_t)i;
    }
    stub_status = status_$ok;
    memset(&info, 0xEE, sizeof(info));

    NETWORK_$RING_INFO(&handle, &info, &st);

    for (i = 0; i < 122; i++) {
        ASSERT_EQ((uint8_t)(i + 6), ((uint8_t *)&info)[i]);
    }
}

TEST(leaves_buffer_untouched_on_error) {
    ring_info_t info;
    status_$t st = 0;
    uint32_t handle = 0;
    unsigned i;

    for (i = 0; i < sizeof(stub_response); i++) {
        stub_response[i] = (uint8_t)i;
    }
    stub_status = status_$network_transmit_failed;
    memset(&info, 0xEE, sizeof(info));

    NETWORK_$RING_INFO(&handle, &info, &st);

    ASSERT_EQ(status_$network_transmit_failed, st);
    for (i = 0; i < 122; i++) {
        ASSERT_EQ(0xEEu, ((uint8_t *)&info)[i]);
    }
}

int main(void) {
    printf("=== NETWORK_$RING_INFO / ring_info_t tests ===\n");

    printf("\nLayout (NETWORK_$PROCESS_PAGING_REQUEST case 0x0E, 0x00E11246..0x00E112D4):\n");
    RUN_TEST(record_is_122_bytes);
    RUN_TEST(header_offsets);
    RUN_TEST(stats_block_is_ring_stats_t);
    RUN_TEST(failure_rec_block);
    RUN_TEST(swdiag_block);
    RUN_TEST(tail_biphase_esb_words);

    printf("\nNETWORK_$RING_INFO behaviour:\n");
    RUN_TEST(sends_command_0x0e);
    RUN_TEST(copies_122_bytes_from_response_offset_6);
    RUN_TEST(leaves_buffer_untouched_on_error);

    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
