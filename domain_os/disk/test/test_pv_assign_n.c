/*
 * disk/test/test_pv_assign_n.c - Unit tests for DISK_$PV_ASSIGN_N (0x00E6C852)
 */

#include <stdio.h>
#include <string.h>

#include "disk/disk_internal.h"

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    printf("  Running %s... ", #name); \
    test_##name(); \
    tests_passed++; \
    printf("PASSED\n"); \
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

static int calls;
static int16_t m_type, m_utype, m_dev, m_unit;
static uint32_t m_blocks_in;
static uint32_t label_in[4];

int16_t DISK_$PV_MOUNT_INTERNAL(int16_t mount_type, int16_t unit_type,
                                uint16_t device, uint16_t unit,
                                uint16_t *vol_idx_ptr, uint32_t *num_blocks_ptr,
                                uint16_t *sec_per_track_ptr, uint16_t *num_heads_ptr,
                                void *pvlabel_info, status_$t *status)
{
    calls++;
    m_type = mount_type; m_utype = unit_type; m_dev = (int16_t)device; m_unit = (int16_t)unit;
    m_blocks_in = *num_blocks_ptr;
    memcpy(label_in, pvlabel_info, 16);
    *vol_idx_ptr = 7;
    *num_blocks_ptr = 0x9999;
    *sec_per_track_ptr = 18;
    *num_heads_ptr = 5;
    ((uint32_t *)pvlabel_info)[0] = 0xCAFE;
    *status = 0x1234;
    return 0;
}

#include "../pv_assign_n.c"

static void run(int16_t utype, uint16_t flags, uint16_t *vol, uint32_t *blocks,
                uint16_t *sec, uint16_t *heads, uint32_t *label, status_$t *st)
{
    int16_t dev = 3, unit = 1;
    calls = 0;
    DISK_$PV_ASSIGN_N(&utype, &dev, &unit, &flags, vol, blocks, sec, heads, label, st);
}

TEST(bad_unit_type)
{
    uint16_t vol = 0xAAAA, sec = 0, heads = 0;
    uint32_t blocks = 0, label[4] = {0};
    status_$t st = 0;
    run(2, 0, &vol, &blocks, &sec, &heads, label, &st);
    ASSERT_EQ(status_$invalid_unit_number, st);
    ASSERT_EQ(0, calls);
    ASSERT_EQ(0xAAAA, vol);
}

TEST(plain_assign)
{
    uint16_t vol = 0, sec = 1, heads = 2;
    uint32_t blocks = 0x55, label[4] = {1, 2, 3, 4};
    status_$t st = 0;
    run(1, 0, &vol, &blocks, &sec, &heads, label, &st);
    ASSERT_EQ(1, calls);
    ASSERT_EQ(1, m_type);
    ASSERT_EQ(1, m_utype);
    ASSERT_EQ(3, m_dev);
    ASSERT_EQ(1, m_unit);
    ASSERT_EQ(0x55, m_blocks_in);
    ASSERT_EQ(1, label_in[0]);
    ASSERT_EQ(0x1234, st);
    ASSERT_EQ(7, vol);
    /* nothing else handed back without flags */
    ASSERT_EQ(0x55, blocks);
    ASSERT_EQ(1, sec);
    ASSERT_EQ(1, label[0]);
}

TEST(flags_no_volx_label_geometry)
{
    uint16_t vol = 0, sec = 1, heads = 2;
    uint32_t blocks = 0x55, label[4] = {1, 2, 3, 4};
    status_$t st = 0;
    run(4, 7, &vol, &blocks, &sec, &heads, label, &st);
    ASSERT_EQ(0, m_type);
    ASSERT_EQ(0xFFFFFFFF, m_blocks_in);
    ASSERT_EQ(0x9999, blocks);
    ASSERT_EQ(18, sec);
    ASSERT_EQ(5, heads);
    ASSERT_EQ(0xCAFE, label[0]);
    ASSERT_EQ(7, vol);
}

int main(void)
{
    printf("test_pv_assign_n:\n");
    RUN_TEST(bad_unit_type);
    RUN_TEST(plain_assign);
    RUN_TEST(flags_no_volx_label_geometry);
    printf("%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
