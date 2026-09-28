/*
 * log/test/test_add.c - LOG_$ADD (0x00E1763E)
 *
 * The log page is a 0x200-word array; ML_$SPIN_LOCK / UNLOCK are mocked.
 * Covers the size rounding, the 1..100 word window, the page-end wrap with
 * its zero sentinel, the head walk over overwritten entries, the mirror
 * into LOG_$LAST_ENTRY, and the dirty flag.
 */

#include <stdio.h>
#include <string.h>

#include "log/log_internal.h"

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    printf("  Running %-44s ", #name); \
    test_##name(); \
    tests_passed++; \
    printf("PASSED\n"); \
} while (0)

#define ASSERT_EQ(expected, actual) do { \
    long long _e = (long long)(expected); \
    long long _a = (long long)(actual); \
    if (_e != _a) { \
        printf("FAILED\n    Expected: %lld, Got: %lld at line %d\n", \
               _e, _a, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while (0)

/* ==========================================================================
 * Mocks and globals
 * ========================================================================== */

uint32_t TIME_$CURRENT_CLOCKH;

static int lock_calls, unlock_calls;
static void *lock_ptr;
static ml_$spin_token_t unlock_token;

ml_$spin_token_t ML_$SPIN_LOCK(void *lockp)
{
    lock_calls++;
    lock_ptr = lockp;
    return 0x1234;
}

void ML_$SPIN_UNLOCK(void *lockp, ml_$spin_token_t token)
{
    (void)lockp;
    unlock_calls++;
    unlock_token = token;
}

#include "../log_data.c"
#include "../add.c"

static int16_t page[0x200];

static void reset(void)
{
    memset(page, 0, sizeof(page));
    page[0] = 0;
    page[1] = 1;
    LOG_$LOGFILE_PTR = page;
    LOG_$STATE.dirty_flag = 0x55;
    LOG_$STATE.current_entry_ptr = NULL;
    memset(&LOG_$LAST_ENTRY, 0, sizeof(LOG_$LAST_ENTRY));
    TIME_$CURRENT_CLOCKH = 0xC10C0000u;
    lock_calls = unlock_calls = 0;
}

/* ==========================================================================
 * Tests
 * ========================================================================== */

/* 0x00E17654: before LOG_$INIT nothing happens at all. */
TEST(no_page_is_a_no_op)
{
    int16_t d[1] = { 1 };
    reset();
    LOG_$LOGFILE_PTR = NULL;
    LOG_$ADD(3, d, 2);
    ASSERT_EQ(0x55, (int8_t)LOG_$STATE.dirty_flag);
    ASSERT_EQ(0, lock_calls);
}

/* A first entry: header at page[2..5], data after, tail advanced, both
 * copies written, dirty set, lock token handed back. */
TEST(first_entry)
{
    int16_t d[3] = { 0x1111, 0x2222, 0x3333 };
    reset();
    LOG_$ADD(7, d, 6);

    ASSERT_EQ(0, page[0]);
    ASSERT_EQ(8, page[1]);              /* 1 + 7 words */
    ASSERT_EQ(7, page[2]);              /* size */
    ASSERT_EQ(7, page[3]);              /* type */
    ASSERT_EQ(0x1111, page[6]);
    ASSERT_EQ(0x3333, page[8]);
    ASSERT_EQ(0xC10C0000u, ((log_entry_header_t *)&page[2])->timestamp);
    ASSERT_EQ((long long)(intptr_t)&page[2],
              (long long)(intptr_t)LOG_$STATE.current_entry_ptr);
    ASSERT_EQ(7, LOG_$LAST_ENTRY.size);
    ASSERT_EQ(7, LOG_$LAST_ENTRY.type);
    ASSERT_EQ(0xC10C0000u, LOG_$LAST_ENTRY.timestamp);
    ASSERT_EQ(0x2222, LOG_$LAST_ENTRY.data[1]);
    ASSERT_EQ(-1, (int8_t)LOG_$STATE.dirty_flag);
    ASSERT_EQ(1, lock_calls);
    ASSERT_EQ((long long)(intptr_t)&LOG_$STATE.spin_lock,
              (long long)(intptr_t)lock_ptr);
    ASSERT_EQ(1, unlock_calls);
    ASSERT_EQ(0x1234, unlock_token);
}

/* 0x00E1765C-0x00E17668: an odd byte count rounds up to whole words. */
TEST(odd_byte_count_rounds_up)
{
    int16_t d[2] = { 0x4444, 0x5555 };
    reset();
    LOG_$ADD(1, d, 3);
    ASSERT_EQ(6, page[2]);              /* 2 data words + 4 */
    ASSERT_EQ(0x5555, page[7]);
    ASSERT_EQ(7, page[1]);
}

/* 0x00E1766C-0x00E17676: the size window.  96 data words (0xC0 bytes)
 * make exactly 100; 0xC1 bytes make 101 and are dropped; a byte count of
 * -9 gives -4 data words -> 0 total, dropped. */
TEST(size_window)
{
    int16_t d[97];
    memset(d, 0x11, sizeof(d));

    reset();
    LOG_$ADD(1, d, 0xC0);
    ASSERT_EQ(100, page[2]);
    ASSERT_EQ(1, lock_calls);

    reset();
    LOG_$ADD(1, d, 0xC1);
    ASSERT_EQ(0, page[2]);
    ASSERT_EQ(0, lock_calls);
    ASSERT_EQ(0x55, (int8_t)LOG_$STATE.dirty_flag);

    reset();
    LOG_$ADD(1, d, -9);
    ASSERT_EQ(0, lock_calls);

    /* -7 bytes: (-7+1)/2 = -3 data words -> 1 word total, accepted */
    reset();
    LOG_$ADD(1, d, -7);
    ASSERT_EQ(1, lock_calls);
    ASSERT_EQ(1, page[2]);
    ASSERT_EQ(2, page[1]);
}

/* 0x00E1768E-0x00E176B8: an entry that would run past index 0x1FE writes
 * the zero sentinel at the old tail and restarts both indices at 1. */
TEST(wrap_at_page_end)
{
    int16_t d[2] = { 0x6666, 0x7777 };
    reset();
    page[0] = 0x100;
    page[1] = 0x1FC;
    page[0x1FD] = 0x5A5A;               /* the slot the sentinel lands in */
    page[2] = 5;                        /* old entries at 1 and 6, which the */
    page[7] = 0x10;                     /* head walk must step over */
    LOG_$ADD(2, d, 4);                  /* 6 words: 0x1FC + 6 - 1 > 0x1FE */

    ASSERT_EQ(0, page[0x1FD]);          /* sentinel at buf[tail+1] */
    ASSERT_EQ(6, page[2]);              /* the entry went to index 1 */
    ASSERT_EQ(7, page[1]);
    /* head 1 lies within the new entry (1..6): over the old 5-word entry
     * to 6, still within, over the 0x10-word one to 0x16, which stops it.
     * (The walk runs BEFORE the entry is written, so a zero size word at
     * index 1 would wrap the head to 1 for ever - see add.c.) */
    ASSERT_EQ(0x16, page[0]);
}

/* 0x00E176BC-0x00E176E0: entries the new one overwrites are skipped by
 * following their size words. */
TEST(head_walks_over_overwritten_entries)
{
    int16_t d[4] = { 1, 2, 3, 4 };
    reset();
    /* three 5-word entries at 1, 6, 11; head at 1, tail at 16 -> now
     * pretend the page wrapped so that head 1 is ahead of tail 1 */
    page[2] = 5; page[7] = 5; page[12] = 5; page[17] = 0;
    page[0] = 1;
    page[1] = 1;
    LOG_$ADD(9, d, 8);                  /* 8 words at 1..8 */

    /* head 1 <= 8: skip to 6; 6 <= 8: skip to 11; 11 > 8: stop */
    ASSERT_EQ(11, page[0]);
    ASSERT_EQ(9, page[1]);
    ASSERT_EQ(8, page[2]);
}

/* A zero size word (the sentinel) or running off the page wraps the head
 * to 1, which then lies before the tail and ends the walk. */
TEST(head_wraps_on_sentinel_and_page_end)
{
    int16_t d[1] = { 0 };
    reset();
    page[0] = 3; page[1] = 3; page[4] = 0;      /* sentinel at head */
    LOG_$ADD(1, d, 2);
    ASSERT_EQ(1, page[0]);
    ASSERT_EQ(8, page[1]);

    reset();
    page[0] = 3; page[1] = 3; page[4] = 0x1FB;  /* 3 + 0x1FB = 0x1FE */
    LOG_$ADD(1, d, 2);
    ASSERT_EQ(1, page[0]);
}

/* 0x00E17746-0x00E17756: a tail landing exactly on 0x1FE stays, one past
 * wraps to 1. */
TEST(tail_wrap)
{
    int16_t d[1] = { 0 };
    reset();
    page[0] = 0; page[1] = 0x1F9;
    LOG_$ADD(1, d, 2);                  /* 5 words: last = 0x1FD */
    ASSERT_EQ(0x1FE, page[1]);

    reset();
    page[0] = 0; page[1] = 0x1FA;
    LOG_$ADD(1, d, 2);                  /* last = 0x1FE, fits; tail 0x1FF */
    ASSERT_EQ(1, page[1]);
}

int main(void)
{
    printf("LOG_$ADD tests\n");
    RUN_TEST(no_page_is_a_no_op);
    RUN_TEST(first_entry);
    RUN_TEST(odd_byte_count_rounds_up);
    RUN_TEST(size_window);
    RUN_TEST(wrap_at_page_end);
    RUN_TEST(head_walks_over_overwritten_entries);
    RUN_TEST(head_wraps_on_sentinel_and_page_end);
    RUN_TEST(tail_wrap);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
