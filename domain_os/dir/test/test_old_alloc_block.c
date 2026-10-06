/*
 * dir/test/test_old_alloc_block.c - dir_$old_alloc_block (0x00E54E10)
 *
 * Pins: the free-list head (+0xC) is taken first and replaced by its
 * +0x36A link; otherwise the high-water mark (+0xA) grows while below the
 * limit (+0x6); the block in-use count is cleared; at the limit 0.
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

#define TEST_SUMMARY() do { \
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed); \
    return tests_failed ? 1 : 0; \
} while (0)

#include "dir/dir_internal.h"

/* A fake old-format directory in an arena the VA macros can reach. */
static uint8_t dirbuf[0x4000] __attribute__((aligned(4)));
#define DVA 0x100
static void w16(uint32_t off, uint16_t v) { *(uint16_t *)(dirbuf + off) = v; }
static uint16_t r16(uint32_t off) { return *(uint16_t *)(dirbuf + off); }
static uint8_t *blk(int k) { return dirbuf + 0x96 * k; }
static dir_$old_slot_t *bslot(int k, int j)
{
    return (dir_$old_slot_t *)(dirbuf + 0x96 * k + 0x30 * j + 0x340);
}
static void dir_setup(uint16_t buckets, uint16_t max_blocks,
                      uint16_t block_slots, uint16_t blocks_used)
{
    memset(dirbuf, 0, sizeof(dirbuf));
    ARCH_HOST_VA_BASE = (uintptr_t)dirbuf - DVA;
    w16(0x02, buckets);
    w16(0x06, max_blocks);
    w16(0x08, block_slots);
    w16(0x0A, blocks_used);
}

#include "../old_alloc_block.c"

TEST(from_free_list)
{
    dir_setup(4, 10, 3, 5);
    w16(0x0C, 4);
    *(uint16_t *)(blk(4) + 0x36A) = 2;
    blk(4)[0x36E] = 9;
    ASSERT_EQ(4, dir_$old_alloc_block(DVA));
    ASSERT_EQ(2, r16(0x0C));
    ASSERT_EQ(0, blk(4)[0x36E]);
    ASSERT_EQ(5, r16(0x0A));
}

TEST(from_high_water)
{
    dir_setup(4, 10, 3, 5);
    blk(6)[0x36E] = 9;
    ASSERT_EQ(6, dir_$old_alloc_block(DVA));
    ASSERT_EQ(6, r16(0x0A));
    ASSERT_EQ(0, blk(6)[0x36E]);
}

TEST(at_limit)
{
    dir_setup(4, 5, 3, 5);
    ASSERT_EQ(0, dir_$old_alloc_block(DVA));
    ASSERT_EQ(5, r16(0x0A));
}

int main(void)
{
    printf("dir_$old_alloc_block tests\n");
    RUN_TEST(from_free_list);
    RUN_TEST(from_high_water);
    RUN_TEST(at_limit);
    TEST_SUMMARY();
}
