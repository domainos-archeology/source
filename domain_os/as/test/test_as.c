/*
 * as/test/test_as.c - Unit tests for the AS_$ (address space) subsystem
 *
 * Self-contained host program: includes the implementation under test
 * directly.  Exercises AS_$GET_INFO, AS_$GET_ADDR and AS_$INIT against
 * mocked MST segment configuration and MMU M68020 flag.
 *
 * The kernel headers are included before any host header so that the
 * Domain/OS definitions of clock_t, uid_t, true/false, etc. win.
 */

#include "as/as_internal.h"

#include <stdio.h>
#include <string.h>

/* ============================================================================
 * Test framework
 * ============================================================================ */

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    int _before = tests_failed; \
    printf("  Running %s... ", #name); \
    test_##name(); \
    if (tests_failed == _before) { \
        tests_passed++; \
        printf("PASSED\n"); \
    } \
} while(0)

#define ASSERT_EQ(expected, actual) do { \
    unsigned long _e = (unsigned long)(expected); \
    unsigned long _a = (unsigned long)(actual); \
    if (_e != _a) { \
        printf("FAILED\n    Expected: 0x%lx (%lu), Got: 0x%lx (%lu) at line %d\n", \
               _e, _e, _a, _a, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while(0)

/* ============================================================================
 * Mock MST segment configuration
 * These values represent a typical M68010 configuration.
 * (mst/mst.h declares them extern; mst_data.c is not linked here.)
 * ============================================================================ */

uint16_t MST_$PRIVATE_A_SIZE = 0x0137;    /* 311 segments = 9.72MB */
uint16_t MST_$SEG_GLOBAL_A = 0x0138;      /* Segment 312 */
uint16_t MST_$GLOBAL_A_SIZE = 0x0060;     /* 96 segments = 3MB */
uint16_t MST_$SEG_PRIVATE_B = 0x0198;     /* Segment 408 */
uint16_t MST_$SEG_GLOBAL_B = 0x01A0;      /* Segment 416 */
uint16_t MST_$GLOBAL_B_SIZE = 0x0140;     /* 320 segments */

/*
 * The MMU_$GLOBALS block (mmu/mmu.h), whose M68020 word is 0xE23D2E on the
 * real machine; AS_$INIT tests the sign of its HIGH byte,
 * `tst.b (0xe23d2e).l'.
 */
MODULE_DATA_DEFINE(mmu_$globals_t, MMU_$GLOBALS, 0x00E23D2C);

/* ============================================================================
 * Implementation under test
 * ============================================================================ */

#include "../as_data.c"
#include "../get_info.c"
#include "../get_addr.c"
#include "../init.c"

/* ========================================================================
 * AS_$GET_INFO Tests
 * ======================================================================== */

/* Test: Get full info structure */
TEST(get_info_full_size) {
    uint8_t buffer[AS_INFO_SIZE];
    int16_t req_size = AS_INFO_SIZE;
    int16_t actual_size = 0;

    AS_$GET_INFO(buffer, &req_size, &actual_size);

    ASSERT_EQ(AS_INFO_SIZE, actual_size);
    /* Copied bytes must match AS_$INFO */
    ASSERT_EQ(0, memcmp(buffer, &AS_$INFO, AS_INFO_SIZE));
}

/* Test: Request larger than available */
TEST(get_info_request_too_large) {
    uint8_t buffer[200];
    int16_t req_size = 200;
    int16_t actual_size = 0;

    AS_$GET_INFO(buffer, &req_size, &actual_size);

    /* Should return only AS_INFO_SIZE bytes */
    ASSERT_EQ(AS_INFO_SIZE, actual_size);
}

/* Test: Request smaller than available */
TEST(get_info_partial) {
    uint8_t buffer[20];
    int16_t req_size = 20;
    int16_t actual_size = 0;

    memset(buffer, 0xAA, sizeof(buffer));
    AS_$GET_INFO(buffer, &req_size, &actual_size);

    ASSERT_EQ(20, actual_size);
    ASSERT_EQ(0, memcmp(buffer, &AS_$INFO, 20));
}

/* Test: Request zero bytes */
TEST(get_info_zero_request) {
    uint8_t buffer[10];
    int16_t req_size = 0;
    int16_t actual_size = 99;

    AS_$GET_INFO(buffer, &req_size, &actual_size);

    ASSERT_EQ(0, actual_size);
}

/* Test: Negative request size */
TEST(get_info_negative_request) {
    uint8_t buffer[10];
    int16_t req_size = -5;
    int16_t actual_size = 99;

    AS_$GET_INFO(buffer, &req_size, &actual_size);

    ASSERT_EQ(0, actual_size);
}

/* ========================================================================
 * AS_$GET_ADDR Tests
 * ======================================================================== */

/* Test: Private A region starts at 0 */
TEST(get_addr_private_a) {
    as_$addr_range_t range;
    int16_t region = AS_REGION_PRIVATE_A;

    AS_$GET_ADDR(&range, &region);

    ASSERT_EQ(0, range.base);
    /* Size = MST_$PRIVATE_A_SIZE << 15 = 0x137 << 15 = 0x9B8000 */
    ASSERT_EQ(0x9B8000, range.size);
}

/* Test: Global A region */
TEST(get_addr_global_a) {
    as_$addr_range_t range;
    int16_t region = AS_REGION_GLOBAL_A;

    AS_$GET_ADDR(&range, &region);

    /* Base = MST_$SEG_GLOBAL_A << 15 = 0x138 << 15 = 0x9C0000 */
    ASSERT_EQ(0x9C0000, range.base);
    /* Size = MST_$GLOBAL_A_SIZE << 15 = 0x60 << 15 = 0x300000 */
    ASSERT_EQ(0x300000, range.size);
}

/* Test: Private B region has fixed 256KB size */
TEST(get_addr_private_b) {
    as_$addr_range_t range;
    int16_t region = AS_REGION_PRIVATE_B;

    AS_$GET_ADDR(&range, &region);

    /* Base = MST_$SEG_PRIVATE_B << 15 = 0x198 << 15 = 0xCC0000 */
    ASSERT_EQ(0xCC0000, range.base);
    /* Private B always has fixed size of 256KB */
    ASSERT_EQ(0x40000, range.size);
}

/* Test: Global B region */
TEST(get_addr_global_b) {
    as_$addr_range_t range;
    int16_t region = AS_REGION_GLOBAL_B;

    AS_$GET_ADDR(&range, &region);

    /* Base = MST_$SEG_GLOBAL_B << 15 = 0x1A0 << 15 = 0xD00000 */
    ASSERT_EQ(0xD00000, range.base);
    /* Size = MST_$GLOBAL_B_SIZE << 15 = 0x140 << 15 = 0xA00000 */
    ASSERT_EQ(0xA00000, range.size);
}

/* Test: Invalid region returns sentinel values */
TEST(get_addr_invalid_region) {
    as_$addr_range_t range;
    int16_t region = 99;  /* Invalid */

    AS_$GET_ADDR(&range, &region);

    ASSERT_EQ(0x7FFFFFFF, range.base);
    ASSERT_EQ(0, range.size);
}

/* Test: Region 4 is also invalid */
TEST(get_addr_region_4_invalid) {
    as_$addr_range_t range;
    int16_t region = 4;  /* Just past valid range */

    AS_$GET_ADDR(&range, &region);

    ASSERT_EQ(0x7FFFFFFF, range.base);
    ASSERT_EQ(0, range.size);
}

/* Test: Negative region is invalid */
TEST(get_addr_negative_region) {
    as_$addr_range_t range;
    int16_t region = -1;

    AS_$GET_ADDR(&range, &region);

    ASSERT_EQ(0x7FFFFFFF, range.base);
    ASSERT_EQ(0, range.size);
}

/* ========================================================================
 * AS_$INIT Tests
 * ======================================================================== */

/* Test: On an M68010 system (flag byte non-negative) AS_$INFO is unchanged */
TEST(init_m68010_no_change) {
    as_$info_t before = AS_$INFO;

    M68020 = 0;
    AS_$INIT();

    ASSERT_EQ(0, memcmp(&before, &AS_$INFO, sizeof(AS_$INFO)));
}

/* Test: a set bit in the LOW byte only (0xE23D2F) is not the flag byte */
TEST(init_low_byte_only_no_change) {
    as_$info_t before = AS_$INFO;

    M68020 = 0x0080;
    AS_$INIT();
    M68020 = 0;

    ASSERT_EQ(0, memcmp(&before, &AS_$INFO, sizeof(AS_$INFO)));
}

/* Test: On an M68020 system (bit 7 of the HIGH byte set) the layout is adjusted */
TEST(init_m68020_adjusts_layout) {
    as_$info_t before = AS_$INFO;

    M68020 = 0xFF00;
    AS_$INIT();
    M68020 = 0;

    ASSERT_EQ(M68020_GLOBAL_A_BASE, AS_$INFO.global_a);
    ASSERT_EQ(M68020_GLOBAL_A_SIZE, AS_$INFO.global_a_size);
    ASSERT_EQ(M68020_GLOBAL_A_BASE, AS_$INFO.m68020_global_a);
    ASSERT_EQ(M68020_GLOBAL_A_SIZE, AS_$INFO.m68020_global_a_size);

    ASSERT_EQ(before.stack_file_low + M68020_AS_OFFSET, AS_$INFO.stack_file_low);
    ASSERT_EQ(before.cr_rec + M68020_AS_OFFSET, AS_$INFO.cr_rec);
    ASSERT_EQ(before.cr_rec_end + M68020_AS_OFFSET, AS_$INFO.cr_rec_end);
    ASSERT_EQ(before.stack_file_high + M68020_AS_OFFSET, AS_$INFO.stack_file_high);
    ASSERT_EQ(before.stack_low + M68020_AS_OFFSET, AS_$INFO.stack_low);
    ASSERT_EQ(before.stack_high + M68020_AS_OFFSET, AS_$INFO.stack_high);
    ASSERT_EQ(before.stack_offset + M68020_AS_OFFSET, AS_$INFO.stack_offset);

    /* cr_rec_file is set to the (adjusted) cr_rec_end */
    ASSERT_EQ(AS_$INFO.cr_rec_end, AS_$INFO.cr_rec_file);

    /* Fields AS_$INIT does not touch are unchanged */
    ASSERT_EQ(before.private_base, AS_$INFO.private_base);
    ASSERT_EQ(before.init_stack_file_size, AS_$INFO.init_stack_file_size);
    ASSERT_EQ(before.cr_rec_file_size, AS_$INFO.cr_rec_file_size);
}

/* Test: Structure layout matches the 92-byte record at 0xE2B914 */
TEST(info_struct_size) {
    ASSERT_EQ(AS_INFO_SIZE, sizeof(as_$info_t));
    ASSERT_EQ(AS_INFO_SIZE, AS_$INFO_SIZE);
}

int main(void) {
    printf("AS_$ subsystem tests\n");

    RUN_TEST(info_struct_size);
    RUN_TEST(get_info_full_size);
    RUN_TEST(get_info_request_too_large);
    RUN_TEST(get_info_partial);
    RUN_TEST(get_info_zero_request);
    RUN_TEST(get_info_negative_request);
    RUN_TEST(get_addr_private_a);
    RUN_TEST(get_addr_global_a);
    RUN_TEST(get_addr_private_b);
    RUN_TEST(get_addr_global_b);
    RUN_TEST(get_addr_invalid_region);
    RUN_TEST(get_addr_region_4_invalid);
    RUN_TEST(get_addr_negative_region);
    /* AS_$INIT mutates the global AS_$INFO; run these last */
    RUN_TEST(init_m68010_no_change);
    RUN_TEST(init_low_byte_only_no_change);
    RUN_TEST(init_m68020_adjusts_layout);

    printf("\nResults: %d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
