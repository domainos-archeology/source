/*
 * dir/test/test_old_reclaim_block.c - dir_$old_reclaim_block (0x00E54E62)
 *
 * Pins: buckets are searched from hash+1 round the table; a chain is
 * walked from its last block towards the head; the first block whose used
 * slots are all type 1 without the no-evict bit has every used slot
 * deleted and the result is dir_$old_alloc_block; when nothing can be
 * evicted the result is the D0 leftover (the chain head * 0x96, or the
 * last bucket number); with no buckets it is the hash argument.
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

static int ndel;
static uint16_t del_block[8], del_slot[8], del_hash[8];

void dir_$old_delete_entry(uint32_t handle, uint16_t slot_idx,
                           uint16_t chain_level, uint16_t hash)
{
    (void)handle;
    if (ndel < 8) {
        del_block[ndel] = slot_idx;
        del_slot[ndel] = chain_level;
        del_hash[ndel] = hash;
    }
    ndel++;
}
short M$OIU$WLW(long dividend, short divisor)
{
    return (short)((uint32_t)dividend % (uint16_t)divisor);
}

#include "../old_alloc_block.c"
#include "../old_reclaim_block.c"

static void chain2(int bucket, int head, int tail)
{
    w16(0x3AA + 2 * bucket, (uint16_t)head);
    *(uint16_t *)(blk(head) + 0x36A) = (uint16_t)tail;
    *(uint16_t *)(blk(tail) + 0x36C) = (uint16_t)head;
}

TEST(evicts_tail_first)
{
    dir_setup(4, 10, 2, 6);
    ndel = 0;
    chain2(2, 3, 4);
    bslot(3, 1)->type = 1;              /* head: evictable too */
    bslot(4, 1)->type = 1;
    bslot(4, 2)->type = 1;
    ASSERT_EQ(7, dir_$old_reclaim_block(DVA, 1));
    ASSERT_EQ(2, ndel);
    ASSERT_EQ(4, del_block[0]);
    ASSERT_EQ(1, del_slot[0]);
    ASSERT_EQ(2, del_slot[1]);
    ASSERT_EQ(2, del_hash[0]);
}

TEST(skips_pinned_and_links)
{
    dir_setup(4, 10, 2, 6);
    ndel = 0;
    chain2(2, 3, 4);
    bslot(3, 2)->type = 1;
    bslot(4, 1)->type = 3;              /* link text slot: not evictable */
    ((uint8_t *)bslot(4, 2))[0x24] = 0x80;
    bslot(4, 2)->type = 1;              /* no-evict bit */
    ASSERT_EQ(7, dir_$old_reclaim_block(DVA, 1));
    ASSERT_EQ(1, ndel);
    ASSERT_EQ(3, del_block[0]);
    ASSERT_EQ(2, del_slot[0]);
}

TEST(nothing_evictable_leaves_d0)
{
    dir_setup(4, 10, 2, 6);
    ndel = 0;
    w16(0x3AA + 2 * 1, 5);              /* bucket 1 is searched last */
    bslot(5, 1)->type = 3;
    ASSERT_EQ(5 * 0x96, dir_$old_reclaim_block(DVA, 1));
    ASSERT_EQ(0, ndel);
}

TEST(empty_table_gives_last_bucket)
{
    dir_setup(4, 10, 2, 6);
    ndel = 0;
    ASSERT_EQ(1, dir_$old_reclaim_block(DVA, 1));
}

TEST(no_buckets_returns_hash)
{
    dir_setup(0, 10, 2, 6);
    ASSERT_EQ(0x1234, dir_$old_reclaim_block(DVA, 0x1234));
}

int main(void)
{
    printf("dir_$old_reclaim_block tests\n");
    RUN_TEST(evicts_tail_first);
    RUN_TEST(skips_pinned_and_links);
    RUN_TEST(nothing_evictable_leaves_d0);
    RUN_TEST(empty_table_gives_last_bucket);
    RUN_TEST(no_buckets_returns_hash);
    TEST_SUMMARY();
}
