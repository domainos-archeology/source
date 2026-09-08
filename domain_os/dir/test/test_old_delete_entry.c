/*
 * dir/test/test_old_delete_entry.c - dir_$old_delete_entry (0x00E555DC)
 *
 * The regression bead source-v76f is about the two link-block words the
 * routine saves before it clears the slot.  The image copies eight bytes out
 * of the entry with two longword moves and then reads two WORDS back:
 *
 *   inline    0x00E55608 lea (0x12,A0),A1 ; 0x00E5560C/0x00E55610 two move.l
 *             0x00E55672 move.w (-0xe,A6) -> entry+0x14
 *             0x00E55680 tst.w (-0xc,A6)  -> entry+0x16
 *   overflow  0x00E55640 lea (0x368,A1),A3
 *             the same two words at entry+0x36A and entry+0x36C
 *
 * The tree used to derive them from 32-bit loads, which only agrees with the
 * image on a big-endian host.  These tests seed the two words with values
 * whose halves differ so a wrong-endian read shows up.
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>

/* ==========================================================================
 * Test framework
 * ========================================================================== */

static int tests_passed = 0;
static int tests_failed = 0;
static int current_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do {                     \
    printf("  Running %-46s ", #name);          \
    current_failed = 0;                         \
    test_##name();                              \
    if (current_failed) { tests_failed++; }     \
    else { tests_passed++; printf("PASSED\n"); }\
} while (0)

#define ASSERT_EQ(expected, actual) do {                                 \
    unsigned long long _e = (unsigned long long)(expected);              \
    unsigned long long _a = (unsigned long long)(actual);                \
    if (_e != _a) {                                                      \
        printf("FAILED\n    Expected 0x%llx, got 0x%llx at line %d\n",   \
               _e, _a, __LINE__);                                        \
        current_failed = 1;                                              \
        return;                                                          \
    }                                                                    \
} while (0)

#include "dir/dir_internal.h"
#include "arch/arch.h"

/* ==========================================================================
 * Mocks
 * ========================================================================== */

#define MAX_FREE_CALLS 8

static int      free_slot_calls;
static uint32_t free_slot_handle[MAX_FREE_CALLS];
static uint16_t free_slot_hash[MAX_FREE_CALLS];
static uint16_t free_slot_index[MAX_FREE_CALLS];

void dir_$old_free_slot(uint32_t handle, uint16_t hash, uint16_t slot_idx)
{
    if (free_slot_calls < MAX_FREE_CALLS) {
        free_slot_handle[free_slot_calls] = handle;
        free_slot_hash[free_slot_calls] = hash;
        free_slot_index[free_slot_calls] = slot_idx;
    }
    free_slot_calls++;
}

/* ==========================================================================
 * Code under test
 * ========================================================================== */

#include "../old_delete_entry.c"

/* ==========================================================================
 * Fixture
 * ========================================================================== */

static uint8_t dir_page[0x2000];
static uint32_t dir_handle;

static void reset(void)
{
    memset(dir_page, 0, sizeof(dir_page));
    free_slot_calls = 0;
    memset(free_slot_handle, 0, sizeof(free_slot_handle));
    memset(free_slot_hash, 0, sizeof(free_slot_hash));
    memset(free_slot_index, 0, sizeof(free_slot_index));
    dir_handle = ARCH_PTR_TO_VA(dir_page);
    *(int16_t *)(dir_page + 0x16) = 5;      /* the entry count */
}

/* Write a big-endian word into the page, the way the image would find it. */
static void put_word(uint32_t off, uint16_t value)
{
    *(uint16_t *)(dir_page + off) = value;
}

/* ==========================================================================
 * Tests
 * ========================================================================== */

/*
 * Inline entry, type 3: the two words the routine frees are entry+0x14 and
 * entry+0x16 - NOT the low half of the longword at +0x12 and the high half of
 * the one at +0x16.
 */
TEST(inline_link_blocks_are_the_words_at_0x14_and_0x16)
{
    const uint32_t entry = 2 * 0x30;

    reset();
    dir_page[entry + 0x11] = 3;             /* a soft link */
    dir_page[entry + 0x0E] = 0xC3;          /* active bit set */
    put_word(entry + 0x12, 0xDEAD);         /* copied but never read back */
    put_word(entry + 0x14, 0x1234);         /* block1 */
    put_word(entry + 0x16, 0x5678);         /* block2 */
    put_word(entry + 0x18, 0xBEEF);         /* copied but never read back */

    dir_$old_delete_entry(dir_handle, 2, 0, 0x77);

    ASSERT_EQ(2, free_slot_calls);
    ASSERT_EQ(0x1234, free_slot_index[0]);
    ASSERT_EQ(0, free_slot_hash[0]);
    ASSERT_EQ(dir_handle, free_slot_handle[0]);
    ASSERT_EQ(0x5678, free_slot_index[1]);
    ASSERT_EQ(0, free_slot_hash[1]);

    /* the slot is cleared and the count drops */
    ASSERT_EQ(0, dir_page[entry + 0x11]);
    ASSERT_EQ(0x43, dir_page[entry + 0x0E]);
    ASSERT_EQ(4, *(int16_t *)(dir_page + 0x16));
}

/* A zero second word skips the second free (0x00E55680 "tst.w"). */
TEST(inline_zero_block2_frees_once)
{
    const uint32_t entry = 1 * 0x30;

    reset();
    dir_page[entry + 0x11] = 3;
    put_word(entry + 0x14, 0x0009);
    put_word(entry + 0x16, 0x0000);

    dir_$old_delete_entry(dir_handle, 1, 0, 0);

    ASSERT_EQ(1, free_slot_calls);
    ASSERT_EQ(9, free_slot_index[0]);
}

/* A non-link entry frees nothing (0x00E5566C "cmpi.w #0x3,D3w"). */
TEST(non_link_entry_frees_nothing)
{
    const uint32_t entry = 1 * 0x30;

    reset();
    dir_page[entry + 0x11] = 1;             /* a hard link, not a soft one */
    put_word(entry + 0x14, 0x0009);
    put_word(entry + 0x16, 0x000A);

    dir_$old_delete_entry(dir_handle, 1, 0, 0);

    ASSERT_EQ(0, free_slot_calls);
    ASSERT_EQ(4, *(int16_t *)(dir_page + 0x16));
}

/*
 * Overflow entry: the bucket is slot_idx * 0x96 and the entry is
 * chain_level * 0x30 inside it; the words live at entry+0x36A and +0x36C.
 * The bucket's slot is freed FIRST, with the caller's hash.
 */
TEST(overflow_link_blocks_are_the_words_at_0x36a_and_0x36c)
{
    const uint32_t bucket = 3 * 0x96;
    const uint32_t entry = bucket + 2 * 0x30;

    reset();
    dir_page[entry + 0x367] = 3;
    dir_page[entry + 0x364] = 0x81;
    dir_page[bucket + 0x36E] = 7;
    put_word(entry + 0x368, 0xDEAD);
    put_word(entry + 0x36A, 0x0ABC);        /* block1 */
    put_word(entry + 0x36C, 0x0DEF);        /* block2 */
    put_word(entry + 0x36E, 0xBEEF);

    dir_$old_delete_entry(dir_handle, 3, 2, 0x55);

    ASSERT_EQ(3, free_slot_calls);
    /* 0x00E5565A: the bucket slot, with the caller's hash */
    ASSERT_EQ(0x55, free_slot_hash[0]);
    ASSERT_EQ(3, free_slot_index[0]);
    /* then the two link blocks, with a hash of zero */
    ASSERT_EQ(0, free_slot_hash[1]);
    ASSERT_EQ(0x0ABC, free_slot_index[1]);
    ASSERT_EQ(0, free_slot_hash[2]);
    ASSERT_EQ(0x0DEF, free_slot_index[2]);

    ASSERT_EQ(0, dir_page[entry + 0x367]);
    ASSERT_EQ(0x01, dir_page[entry + 0x364]);
    ASSERT_EQ(4, *(int16_t *)(dir_page + 0x16));
}

/*
 * The chain count at bucket+0x36E is decremented as a BYTE
 * (0x00E55656 "subq.b #0x1").  Note it overlaps the word the copy reads as
 * reserved_06 for chain level 2, which is why the count is checked on a
 * bucket whose entry is elsewhere.
 */
TEST(overflow_decrements_the_bucket_chain_count)
{
    const uint32_t bucket = 1 * 0x96;
    const uint32_t entry = bucket + 1 * 0x30;

    reset();
    dir_page[entry + 0x367] = 1;
    dir_page[bucket + 0x36E] = 4;

    dir_$old_delete_entry(dir_handle, 1, 1, 0x11);

    ASSERT_EQ(3, dir_page[bucket + 0x36E]);
    ASSERT_EQ(1, free_slot_calls);
    ASSERT_EQ(0x11, free_slot_hash[0]);
    ASSERT_EQ(1, free_slot_index[0]);
}

/* The record really is eight bytes with the two words in the middle. */
TEST(link_refs_record_layout)
{
    ASSERT_EQ(8, sizeof(dir_$old_link_refs_t));
    ASSERT_EQ(2, offsetof(dir_$old_link_refs_t, block1));
    ASSERT_EQ(4, offsetof(dir_$old_link_refs_t, block2));
}

int main(void)
{
    ARCH_HOST_VA_BASE = (uintptr_t)dir_page - 0x1000;
    printf("dir_$old_delete_entry tests\n");
    RUN_TEST(inline_link_blocks_are_the_words_at_0x14_and_0x16);
    RUN_TEST(inline_zero_block2_frees_once);
    RUN_TEST(non_link_entry_frees_nothing);
    RUN_TEST(overflow_link_blocks_are_the_words_at_0x36a_and_0x36c);
    RUN_TEST(overflow_decrements_the_bucket_chain_count);
    RUN_TEST(link_refs_record_layout);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
