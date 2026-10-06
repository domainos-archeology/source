/*
 * dir/test/test_old_find_overflow_slot.c - dir_$old_find_overflow_slot
 * (0x00E54F8A) with its chain helpers (0x00E54D10 push, 0x00E54D62
 * unlink)
 *
 * Pins: a free slot on the chain is returned with its no-evict bit
 * cleared, and in replace mode its block moves to the chain head; a full
 * chain takes a fresh block (cleared, type 1, slot 1, pushed on the head);
 * without one, plain mode fails, replace mode evicts the first plain
 * type-1 slot from the tail and moves that block to the head, and falls
 * back on dir_$old_reclaim_block.
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

static uint16_t alloc_result, reclaim_result;
static int nalloc, nreclaim, ndel;
static uint16_t del_block, del_slot, del_hash;

uint16_t dir_$old_alloc_block(uint32_t handle) { (void)handle; nalloc++; return alloc_result; }
uint16_t dir_$old_reclaim_block(uint32_t handle, uint16_t hash)
{
    (void)handle; (void)hash;
    nreclaim++;
    return reclaim_result;
}
void dir_$old_delete_entry(uint32_t handle, uint16_t slot_idx,
                           uint16_t chain_level, uint16_t hash)
{
    (void)handle;
    ndel++;
    del_block = slot_idx;
    del_slot = chain_level;
    del_hash = hash;
}

#include "../old_find_overflow_slot.c"

#define HASH 3

/* chain HASH: head 4 -> 6 (both 2 slots) */
static void setup(void)
{
    dir_setup(8, 10, 2, 6);
    w16(0x3AA + 2 * HASH, 4);
    *(uint16_t *)(blk(4) + 0x36A) = 6;
    *(uint16_t *)(blk(6) + 0x36C) = 4;
    blk(4)[0x36E] = 2;
    bslot(4, 1)->type = 1;
    bslot(4, 2)->type = 1;
    blk(6)[0x36E] = 1;
    bslot(6, 1)->type = 1;
    alloc_result = reclaim_result = 0;
    nalloc = nreclaim = ndel = 0;
}

TEST(free_slot_plain)
{
    uint16_t b = 0, s = 0;
    setup();
    ((uint8_t *)bslot(6, 2))[0x24] = 0x80;
    ASSERT_EQ(0xFF, (uint8_t)dir_$old_find_overflow_slot(DVA, HASH, 0, &b, &s));
    ASSERT_EQ(6, b);
    ASSERT_EQ(2, s);
    ASSERT_EQ(0, ((uint8_t *)bslot(6, 2))[0x24]);
    ASSERT_EQ(4, r16(0x3AA + 2 * HASH));        /* not moved */
}

TEST(free_slot_replace_moves_to_head)
{
    uint16_t b = 0, s = 0;
    setup();
    ASSERT_EQ(0xFF, (uint8_t)dir_$old_find_overflow_slot(DVA, HASH, (int8_t)0xFF, &b, &s));
    ASSERT_EQ(6, b);
    ASSERT_EQ(6, r16(0x3AA + 2 * HASH));
    ASSERT_EQ(4, *(uint16_t *)(blk(6) + 0x36A));
    ASSERT_EQ(0, *(uint16_t *)(blk(6) + 0x36C));
    ASSERT_EQ(6, *(uint16_t *)(blk(4) + 0x36C));
    ASSERT_EQ(0, *(uint16_t *)(blk(4) + 0x36A));
}

TEST(full_chain_new_block)
{
    uint16_t b = 0, s = 0;
    setup();
    blk(6)[0x36E] = 2;
    bslot(6, 2)->type = 1;
    alloc_result = 5;
    blk(5)[0x36E] = 7;
    bslot(5, 2)->type = 9;
    ((uint8_t *)bslot(5, 1))[0x24] = 0x80;
    ASSERT_EQ(0xFF, (uint8_t)dir_$old_find_overflow_slot(DVA, HASH, 0, &b, &s));
    ASSERT_EQ(5, b);
    ASSERT_EQ(1, s);
    ASSERT_EQ(0, blk(5)[0x36E]);
    ASSERT_EQ(1, blk(5)[0x36F]);
    ASSERT_EQ(0, bslot(5, 2)->type);
    ASSERT_EQ(0, ((uint8_t *)bslot(5, 1))[0x24]);
    ASSERT_EQ(5, r16(0x3AA + 2 * HASH));
    ASSERT_EQ(4, *(uint16_t *)(blk(5) + 0x36A));
    ASSERT_EQ(5, *(uint16_t *)(blk(4) + 0x36C));
}

TEST(full_plain_fails)
{
    uint16_t b = 0, s = 0;
    setup();
    blk(6)[0x36E] = 2;
    bslot(6, 2)->type = 1;
    ASSERT_EQ(0, dir_$old_find_overflow_slot(DVA, HASH, 0, &b, &s));
    ASSERT_EQ(0, b);
    ASSERT_EQ(0, ndel);
}

TEST(full_replace_evicts_from_tail)
{
    uint16_t b = 0, s = 0;
    setup();
    blk(6)[0x36E] = 2;
    bslot(6, 1)->type = 3;                      /* link text: skipped */
    bslot(6, 2)->type = 1;
    ASSERT_EQ(0xFF, (uint8_t)dir_$old_find_overflow_slot(DVA, HASH, (int8_t)0xFF, &b, &s));
    ASSERT_EQ(1, ndel);
    ASSERT_EQ(6, del_block);
    ASSERT_EQ(2, del_slot);
    ASSERT_EQ(HASH, del_hash);
    ASSERT_EQ(6, b);
    ASSERT_EQ(2, s);
    ASSERT_EQ(6, r16(0x3AA + 2 * HASH));
}

TEST(full_replace_falls_back_on_reclaim)
{
    uint16_t b = 0, s = 0;
    setup();
    blk(6)[0x36E] = 2;
    ((uint8_t *)bslot(4, 1))[0x24] = 0x80;
    ((uint8_t *)bslot(4, 2))[0x24] = 0x80;
    bslot(6, 1)->type = 3;
    bslot(6, 2)->type = 3;
    reclaim_result = 0;
    ASSERT_EQ(0, dir_$old_find_overflow_slot(DVA, HASH, (int8_t)0xFF, &b, &s));
    ASSERT_EQ(1, nreclaim);
    reclaim_result = 2;
    ASSERT_EQ(0xFF, (uint8_t)dir_$old_find_overflow_slot(DVA, HASH, (int8_t)0xFF, &b, &s));
    ASSERT_EQ(2, b);
    ASSERT_EQ(1, s);
    ASSERT_EQ(2, r16(0x3AA + 2 * HASH));
}

int main(void)
{
    printf("dir_$old_find_overflow_slot tests\n");
    RUN_TEST(free_slot_plain);
    RUN_TEST(free_slot_replace_moves_to_head);
    RUN_TEST(full_chain_new_block);
    RUN_TEST(full_plain_fails);
    RUN_TEST(full_replace_evicts_from_tail);
    RUN_TEST(full_replace_falls_back_on_reclaim);
    TEST_SUMMARY();
}
