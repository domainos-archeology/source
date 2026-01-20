/*
 * Unit tests for AS_$ subsystem
 *
 * Tests for address space information and region queries.
 */

#include "as/as.h"
#include "mst/mst.h"

/*
 * Mock MST segment configuration for testing
 * These values represent a typical M68010 configuration
 */
#ifdef TESTING
uint16_t MST_$PRIVATE_A_SIZE = 0x0137;    /* 311 segments = 9.72MB */
uint16_t MST_$SEG_GLOBAL_A = 0x0138;      /* Segment 312 */
uint16_t MST_$GLOBAL_A_SIZE = 0x0060;     /* 96 segments = 3MB */
uint16_t MST_$SEG_PRIVATE_B = 0x0198;     /* Segment 408 */
uint16_t MST_$SEG_GLOBAL_B = 0x01A0;      /* Segment 416 */
uint16_t MST_$GLOBAL_B_SIZE = 0x0140;     /* 320 segments */
#endif

/* ========================================================================
 * AS_$GET_INFO Tests
 * ======================================================================== */

/* Test: Get full info structure */
void test_get_info_full_size(void) {
    uint8_t buffer[AS_INFO_SIZE];
    int16_t req_size = AS_INFO_SIZE;
    int16_t actual_size = 0;

    AS_$GET_INFO(buffer, &req_size, &actual_size);

    ASSERT_EQ(actual_size, AS_INFO_SIZE);
    /* First bytes should match AS_$INFO */
    ASSERT_EQ(buffer[0], ((uint8_t*)&AS_$INFO)[0]);
    ASSERT_EQ(buffer[1], ((uint8_t*)&AS_$INFO)[1]);
}

/* Test: Request larger than available */
void test_get_info_request_too_large(void) {
    uint8_t buffer[200];
    int16_t req_size = 200;
    int16_t actual_size = 0;

    AS_$GET_INFO(buffer, &req_size, &actual_size);

    /* Should return only AS_INFO_SIZE bytes */
    ASSERT_EQ(actual_size, AS_INFO_SIZE);
}

/* Test: Request smaller than available */
void test_get_info_partial(void) {
    uint8_t buffer[20];
    int16_t req_size = 20;
    int16_t actual_size = 0;

    AS_$GET_INFO(buffer, &req_size, &actual_size);

    ASSERT_EQ(actual_size, 20);
}

/* Test: Request zero bytes */
void test_get_info_zero_request(void) {
    uint8_t buffer[10];
    int16_t req_size = 0;
    int16_t actual_size = 99;

    AS_$GET_INFO(buffer, &req_size, &actual_size);

    ASSERT_EQ(actual_size, 0);
}

/* Test: Negative request size */
void test_get_info_negative_request(void) {
    uint8_t buffer[10];
    int16_t req_size = -5;
    int16_t actual_size = 99;

    AS_$GET_INFO(buffer, &req_size, &actual_size);

    ASSERT_EQ(actual_size, 0);
}

/* ========================================================================
 * AS_$GET_ADDR Tests
 * ======================================================================== */

/* Test: Private A region starts at 0 */
void test_get_addr_private_a(void) {
    as_$addr_range_t range;
    int16_t region = AS_REGION_PRIVATE_A;

    AS_$GET_ADDR(&range, &region);

    ASSERT_EQ(range.base, 0);
    /* Size = MST_$PRIVATE_A_SIZE << 15 = 0x137 << 15 = 0x9B8000 */
    ASSERT_EQ(range.size, (uint32_t)MST_$PRIVATE_A_SIZE << 15);
}

/* Test: Global A region */
void test_get_addr_global_a(void) {
    as_$addr_range_t range;
    int16_t region = AS_REGION_GLOBAL_A;

    AS_$GET_ADDR(&range, &region);

    /* Base = MST_$SEG_GLOBAL_A << 15 = 0x138 << 15 = 0x9C0000 */
    ASSERT_EQ(range.base, (uint32_t)MST_$SEG_GLOBAL_A << 15);
    /* Size = MST_$GLOBAL_A_SIZE << 15 = 0x60 << 15 = 0x300000 */
    ASSERT_EQ(range.size, (uint32_t)MST_$GLOBAL_A_SIZE << 15);
}

/* Test: Private B region has fixed 256KB size */
void test_get_addr_private_b(void) {
    as_$addr_range_t range;
    int16_t region = AS_REGION_PRIVATE_B;

    AS_$GET_ADDR(&range, &region);

    /* Base = MST_$SEG_PRIVATE_B << 15 = 0x198 << 15 = 0xCC0000 */
    ASSERT_EQ(range.base, (uint32_t)MST_$SEG_PRIVATE_B << 15);
    /* Private B always has fixed size of 256KB */
    ASSERT_EQ(range.size, 0x40000);
}

/* Test: Global B region */
void test_get_addr_global_b(void) {
    as_$addr_range_t range;
    int16_t region = AS_REGION_GLOBAL_B;

    AS_$GET_ADDR(&range, &region);

    /* Base = MST_$SEG_GLOBAL_B << 15 = 0x1A0 << 15 = 0xD00000 */
    ASSERT_EQ(range.base, (uint32_t)MST_$SEG_GLOBAL_B << 15);
    /* Size = MST_$GLOBAL_B_SIZE << 15 */
    ASSERT_EQ(range.size, (uint32_t)MST_$GLOBAL_B_SIZE << 15);
}

/* Test: Invalid region returns sentinel values */
void test_get_addr_invalid_region(void) {
    as_$addr_range_t range;
    int16_t region = 99;  /* Invalid */

    AS_$GET_ADDR(&range, &region);

    ASSERT_EQ(range.base, 0x7FFFFFFF);
    ASSERT_EQ(range.size, 0);
}

/* Test: Region 4 is also invalid */
void test_get_addr_region_4_invalid(void) {
    as_$addr_range_t range;
    int16_t region = 4;  /* Just past valid range */

    AS_$GET_ADDR(&range, &region);

    ASSERT_EQ(range.base, 0x7FFFFFFF);
    ASSERT_EQ(range.size, 0);
}

/* Test: Negative region is invalid */
void test_get_addr_negative_region(void) {
    as_$addr_range_t range;
    int16_t region = -1;

    AS_$GET_ADDR(&range, &region);

    /* Negative values should be treated as invalid (>= 4) */
    ASSERT_EQ(range.base, 0x7FFFFFFF);
    ASSERT_EQ(range.size, 0);
}
