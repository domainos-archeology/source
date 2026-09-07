/*
 * pkt/test/test_pkt_data.c - pkt_$data_t layout and image tests (source-wv5s)
 *
 * The layout assertions in pkt_internal.h only fire under ARCH_M68K, so this
 * test re-checks them on the host and compares every initialiser in
 * pkt/pkt_data.c against the loaded image, read with
 * "gsk read 0x00E24C9C 0x100":
 *
 *   0x00E24CEC  +0x50  00 00 00 00 00 00 00 01  00 00 00 01 00 00 00 00
 *   0x00E24CFC  +0x60  00 00 00 00 00 00 00 00  00 10 00 02 00 02 80 31
 *   0x00E24D0C  +0x70  ff ff 00 00 ff ff 00 00  00 00 00 00 00 00 00 00
 *   0x00E24D1C  +0x80  00 00 00 00 00 00 00 00  00 20 00 02 00 02 80 31
 *   0x00E24D2C  +0x90  ff ff 00 00 ff ff 00 00  00 00 00 00 00 00 00 00
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

#include "pkt/pkt_internal.h"

int8_t NETWORK_$LOOPBACK_FLAG;

#include "../pkt_data.c"

#define OFF(field) ((size_t)((uint8_t *)&(field) - (uint8_t *)PKT_$DATA))

/* ==========================================================================
 * Tests
 * ========================================================================== */

/* The offsets every PKT function addresses off A5 = 0x00E24C9C. */
TEST(recovered_offsets)
{
    ASSERT_EQ(0x00, OFF(PKT_$DATA->missing_nodes[0].node_id));
    ASSERT_EQ(0x50, OFF(PKT_$DATA->spin_lock));
    ASSERT_EQ(0x54, OFF(PKT_$DATA->visibility_seq));
    ASSERT_EQ(0x58, OFF(PKT_$DATA->n_missing));
    ASSERT_EQ(0x5A, OFF(PKT_$DATA->ping_req_hdr));
    ASSERT_EQ(0x5C, OFF(PKT_$DATA->short_id));
    ASSERT_EQ(0x60, OFF(PKT_$DATA->long_id));
    ASSERT_EQ(0x64, OFF(PKT_$DATA->default_flags));
    ASSERT_EQ(0x68, OFF(PKT_$DATA->ping_template));
    ASSERT_EQ(0x88, OFF(PKT_$DATA->ping_reply_info));
    ASSERT_EQ(0xA8, sizeof(pkt_$data_t));

    /* The two info records are exactly 0x20 apart, which is what makes
     * "pea (0x68,A5)" and "pea (0x88,A5)" both land on one. */
    ASSERT_EQ(0x20, sizeof(pkt_$info_t));
    ASSERT_EQ(0x20, OFF(PKT_$DATA->ping_reply_info) - OFF(PKT_$DATA->ping_template));
}

/* pkt_$info_t as PKT_$BLD_INTERNET_HDR reads it (0x00E1206A onward). */
TEST(info_record_offsets)
{
    pkt_$info_t info;

    ASSERT_EQ(0x00, (size_t)((uint8_t *)&info.flags - (uint8_t *)&info));
    ASSERT_EQ(0x02, (size_t)((uint8_t *)&info.routing_type - (uint8_t *)&info));
    ASSERT_EQ(0x04, (size_t)((uint8_t *)&info.addr_type - (uint8_t *)&info));
    ASSERT_EQ(0x06, (size_t)((uint8_t *)&info.protocol - (uint8_t *)&info));
    ASSERT_EQ(0x08, (size_t)((uint8_t *)&info.retry_limit - (uint8_t *)&info));
    ASSERT_EQ(0x0A, (size_t)((uint8_t *)&info.field_0a - (uint8_t *)&info));
    ASSERT_EQ(0x0C, (size_t)((uint8_t *)&info.field_0c - (uint8_t *)&info));
    ASSERT_EQ(0x0E, (size_t)((uint8_t *)&info.addr - (uint8_t *)&info));
}

/* Every initialiser against the image bytes quoted at the top of this file. */
TEST(initialisers_match_the_image)
{
    int i;

    for (i = 0; i < PKT_MAX_MISSING_NODES; i++) {
        ASSERT_EQ(0, PKT_$DATA->missing_nodes[i].node_id);
        ASSERT_EQ(0, PKT_$DATA->missing_nodes[i].seq_number);
    }

    ASSERT_EQ(0, PKT_$DATA->spin_lock);
    ASSERT_EQ(1, PKT_$DATA->visibility_seq);   /* 0x00E24CF0: 00 00 00 01 */
    ASSERT_EQ(0, PKT_$DATA->n_missing);
    ASSERT_EQ(1, PKT_$DATA->ping_req_hdr);     /* 0x00E24CF6: 00 01 */
    ASSERT_EQ(0, PKT_$DATA->short_id);         /* 0x00E24CF8: 00 00 */
    ASSERT_EQ(0, PKT_$DATA->long_id);          /* 0x00E24CFC: 00 00 00 00 */
    ASSERT_EQ(0, PKT_$DATA->default_flags);

    ASSERT_EQ(0x0010, PKT_$DATA->ping_template.flags);
    ASSERT_EQ(2, PKT_$DATA->ping_template.routing_type);
    ASSERT_EQ(2, PKT_$DATA->ping_template.addr_type);
    ASSERT_EQ(0x8031, PKT_$DATA->ping_template.protocol);
    ASSERT_EQ(0xFFFF, PKT_$DATA->ping_template.retry_limit);
    ASSERT_EQ(0, PKT_$DATA->ping_template.field_0a);
    ASSERT_EQ(0xFFFF, PKT_$DATA->ping_template.field_0c);

    ASSERT_EQ(0x0020, PKT_$DATA->ping_reply_info.flags);
    ASSERT_EQ(2, PKT_$DATA->ping_reply_info.routing_type);
    ASSERT_EQ(2, PKT_$DATA->ping_reply_info.addr_type);
    ASSERT_EQ(0x8031, PKT_$DATA->ping_reply_info.protocol);
    ASSERT_EQ(0xFFFF, PKT_$DATA->ping_reply_info.retry_limit);
    ASSERT_EQ(0, PKT_$DATA->ping_reply_info.field_0a);
    ASSERT_EQ(0xFFFF, PKT_$DATA->ping_reply_info.field_0c);

    for (i = 0; i < 16; i++) {
        ASSERT_EQ(0, PKT_$DATA->ping_template.addr[i]);
        ASSERT_EQ(0, PKT_$DATA->ping_reply_info.addr[i]);
    }
}

/*
 * The image's retry_limit of 0xFFFF is what PKT_$SEND_INTERNET's
 * "tst.w (0x8,A4) / bne" (0x00E126A0) treats as "non-zero", and the
 * "cmpi.w #-0x1,D4w" that follows (0x00E1271E) then still replaces it with
 * the builder's hint - so both records ping with the hint, not with 65535
 * attempts.
 */
TEST(image_retry_limit_is_the_no_limit_sentinel)
{
    ASSERT_EQ(0xFFFF, PKT_$DATA->ping_template.retry_limit);
    ASSERT_EQ(0xFFFF, PKT_$DATA->ping_reply_info.retry_limit);
}

int main(void)
{
    printf("pkt_$data_t layout tests\n");

    RUN_TEST(recovered_offsets);
    RUN_TEST(info_record_offsets);
    RUN_TEST(initialisers_match_the_image);
    RUN_TEST(image_retry_limit_is_the_no_limit_sentinel);

    printf("%d tests, %d failed\n", tests_run, tests_failed);
    return tests_failed != 0;
}
