/*
 * disk/test/test_pv_assign.c - Unit tests for DISK_$PV_ASSIGN (0x00E6C95C)
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

static uint16_t n_flags;
static uint32_t *n_blocks_ptr;
static uint16_t *n_sec_ptr, *n_heads_ptr;

void DISK_$PV_ASSIGN_N(int16_t *unit_type_ptr, int16_t *device_ptr,
                       int16_t *unit_ptr, uint16_t *flags_ptr,
                       uint16_t *vol_idx_ptr, uint32_t *num_blocks_ptr,
                       uint16_t *sec_per_track_ptr, uint16_t *num_heads_ptr,
                       uint32_t *pvlabel_info, status_$t *status)
{
    (void)unit_type_ptr; (void)device_ptr; (void)unit_ptr;
    n_flags = *flags_ptr;
    n_blocks_ptr = num_blocks_ptr;
    n_sec_ptr = sec_per_track_ptr;
    n_heads_ptr = num_heads_ptr;
    *vol_idx_ptr = 4;
    *num_blocks_ptr = 0x7777;
    pvlabel_info[0] = 0x11223344;
    pvlabel_info[1] = 0x55667788;
    *status = 9;
}

#include "../pv_assign.c"

static uint8_t arena[0x100];

TEST(positive_info_assign_only)
{
    int16_t ut = 1, dev = 0, unit = 0;
    uint16_t vol = 0, sec = 0, heads = 0;
    int32_t info = 5;
    status_$t st = 0;
    DISK_$PV_ASSIGN(&ut, &dev, &unit, &vol, &info, &sec, &heads, &st);
    ASSERT_EQ(1, n_flags);
    ASSERT_EQ((unsigned long)&info, (unsigned long)n_blocks_ptr);
    ASSERT_EQ((unsigned long)&sec, (unsigned long)n_sec_ptr);
    ASSERT_EQ((unsigned long)&heads, (unsigned long)n_heads_ptr);
    ASSERT_EQ(0x7777, info);
    ASSERT_EQ(4, vol);
    ASSERT_EQ(9, st);
}

TEST(zero_info_asks_geometry)
{
    int16_t ut = 1, dev = 0, unit = 0;
    uint16_t vol = 0, sec = 0, heads = 0;
    int32_t info = 0;
    status_$t st = 0;
    DISK_$PV_ASSIGN(&ut, &dev, &unit, &vol, &info, &sec, &heads, &st);
    ASSERT_EQ(5, n_flags);
}

/* info < 0: -info is the VA that receives the first six label bytes */
TEST(negative_info_returns_label)
{
    int16_t ut = 1, dev = 0, unit = 0;
    uint16_t vol = 0, sec = 0, heads = 0;
    int32_t info;
    status_$t st = 0;
    ARCH_HOST_VA_BASE = (uintptr_t)arena;
    memset(arena, 0, sizeof arena);
    info = -(int32_t)0x40;
    DISK_$PV_ASSIGN(&ut, &dev, &unit, &vol, &info, &sec, &heads, &st);
    ASSERT_EQ(7, n_flags);
    ASSERT_EQ(0x11223344, *(uint32_t *)(arena + 0x40));
    ASSERT_EQ(0x5566, *(uint16_t *)(arena + 0x44));
    ASSERT_EQ(0, *(uint16_t *)(arena + 0x46));
}

int main(void)
{
    printf("test_pv_assign:\n");
    RUN_TEST(positive_info_assign_only);
    RUN_TEST(zero_info_asks_geometry);
    RUN_TEST(negative_info_returns_label);
    printf("%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
