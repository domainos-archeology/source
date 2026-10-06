/*
 * dir/test/test_old_add_link_entry.c - dir_$old_add_link_entry (0x00E5545C)
 *
 * Pins: the text goes into one type-3 block (two past 0x90 bytes) from
 * block+0x370, the first copy running one byte past a short text; the
 * entry is added as type 3 with the {len, block1, block2, 0} record and the
 * replace flag; no block (plain mode) is status_$directory_is_full; replace
 * mode asks dir_$old_reclaim_block with hint len mod buckets; a failed add
 * frees the blocks.
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

static uint16_t reclaim_result, reclaim_hint_seen;
static int nreclaim, nadd, nfree;
static uint16_t free_seen[4];
static status_$t add_status;
static dir_$old_link_refs_t rec_seen;
static uint16_t add_type_seen;
static boolean add_flag_seen;

uint16_t dir_$old_reclaim_block(uint32_t handle, uint16_t hash)
{
    (void)handle;
    nreclaim++;
    reclaim_hint_seen = hash;
    return reclaim_result;
}
void dir_$old_free_slot(uint32_t handle, uint16_t hash, uint16_t slot_idx)
{
    (void)handle; (void)hash;
    if (nfree < 4) free_seen[nfree] = slot_idx;
    nfree++;
}
void dir_$old_add_entry(uid_t *dir_uid, uint32_t handle, uint8_t *name,
                        uint16_t name_len, uint16_t type, void *uid_data,
                        boolean replace_flag, uint8_t *result,
                        status_$t *status_ret)
{
    (void)dir_uid; (void)handle; (void)name; (void)name_len; (void)result;
    nadd++;
    add_type_seen = type;
    add_flag_seen = replace_flag;
    rec_seen = *(dir_$old_link_refs_t *)uid_data;
    *status_ret = add_status;
}

#include "../old_alloc_block.c"
#include "../old_add_link_entry.c"

static uint8_t text[0x140];
static uid_t d = { 1, 2 };
static uint8_t result[4];

static void setup(uint16_t max_blocks)
{
    int i;
    dir_setup(7, max_blocks, 3, 2);
    for (i = 0; i < (int)sizeof(text); i++) text[i] = (uint8_t)(i + 1);
    reclaim_result = 0;
    nreclaim = nadd = nfree = 0;
    add_status = 0;
}

TEST(short_text)
{
    status_$t st = 9;
    setup(10);
    dir_$old_add_link_entry(&d, DVA, (uint8_t *)"ln", 2, text, 5, 0, result, &st);
    ASSERT_EQ(0, st);
    ASSERT_EQ(3, blk(3)[0x36F]);
    ASSERT_EQ(0, memcmp(blk(3) + 0x370, text, 6));   /* one byte past */
    ASSERT_EQ(0, blk(3)[0x370 + 6]);
    ASSERT_EQ(1, nadd);
    ASSERT_EQ(3, add_type_seen);
    ASSERT_EQ(5, rec_seen.text_len);
    ASSERT_EQ(3, rec_seen.block1);
    ASSERT_EQ(0, rec_seen.block2);
    ASSERT_EQ(0, rec_seen.reserved_06);
}

TEST(long_text_two_blocks)
{
    status_$t st = 9;
    setup(10);
    dir_$old_add_link_entry(&d, DVA, (uint8_t *)"ln", 2, text, 0xA0, 0, result, &st);
    ASSERT_EQ(0, st);
    ASSERT_EQ(0, memcmp(blk(3) + 0x370, text, 0x90));
    ASSERT_EQ(3, blk(4)[0x36F]);
    ASSERT_EQ(0, memcmp(blk(4) + 0x370, text + 0x90, 0x10));
    ASSERT_EQ(0, blk(4)[0x370 + 0x10]);             /* exact end */
    ASSERT_EQ(3, rec_seen.block1);
    ASSERT_EQ(4, rec_seen.block2);
}

TEST(full_plain)
{
    status_$t st = 9;
    setup(2);
    dir_$old_add_link_entry(&d, DVA, (uint8_t *)"ln", 2, text, 5, 0, result, &st);
    ASSERT_EQ(status_$directory_is_full, st);
    ASSERT_EQ(0, nreclaim);
    ASSERT_EQ(0, nadd);
}

TEST(full_replace_reclaims)
{
    status_$t st = 9;
    setup(2);
    reclaim_result = 1;
    dir_$old_add_link_entry(&d, DVA, (uint8_t *)"ln", 2, text, 9, (boolean)0xFF,
                            result, &st);
    ASSERT_EQ(0, st);
    ASSERT_EQ(1, nreclaim);
    ASSERT_EQ(2, reclaim_hint_seen);                /* 9 mod 7 */
    ASSERT_EQ(1, rec_seen.block1);
    ASSERT_EQ(0xFF, (uint8_t)add_flag_seen);
}

TEST(second_block_missing_frees_first)
{
    status_$t st = 9;
    setup(3);
    dir_$old_add_link_entry(&d, DVA, (uint8_t *)"ln", 2, text, 0xA0, 0, result, &st);
    ASSERT_EQ(status_$directory_is_full, st);
    ASSERT_EQ(1, nfree);
    ASSERT_EQ(3, free_seen[0]);
    ASSERT_EQ(0, nadd);
}

TEST(add_fails_frees_both)
{
    status_$t st = 9;
    setup(10);
    add_status = status_$name_already_exists;
    dir_$old_add_link_entry(&d, DVA, (uint8_t *)"ln", 2, text, 0xA0, 0, result, &st);
    ASSERT_EQ(status_$name_already_exists, st);
    ASSERT_EQ(2, nfree);
    ASSERT_EQ(3, free_seen[0]);
    ASSERT_EQ(4, free_seen[1]);
}

int main(void)
{
    printf("dir_$old_add_link_entry tests\n");
    RUN_TEST(short_text);
    RUN_TEST(long_text_two_blocks);
    RUN_TEST(full_plain);
    RUN_TEST(full_replace_reclaims);
    RUN_TEST(second_block_missing_frees_first);
    RUN_TEST(add_fails_frees_both);
    TEST_SUMMARY();
}
