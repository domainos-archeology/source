/*
 * dbuf/test/test_set_buff.c - Unit tests for DBUF_$SET_BUFF (0x00E3A8B6)
 */

#include <stdio.h>
#include <string.h>
#include <setjmp.h>

#include "dbuf/dbuf_internal.h"

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

ec_$eventcount_t dbuf_$eventcount;
dbuf_$entry_t DBUF[DBUF_MAX_BUFFERS];
uint32_t DBUF_SPIN_LOCK;
uint32_t dbuf_$head;
uint16_t dbuf_$waiters;
uint16_t dbuf_$count;
uint16_t DBUF_$TROUBLE;
const status_$t OS_DBUF_bad_free_err = 0x000C0002;
const status_$t OS_DBUF_bad_ptr_err = 0x000C0001;

static int spin_locks, spin_unlocks, advances, writes;
static uint32_t write_hdr[8];
static int16_t write_vol;
static status_$t write_status;
static jmp_buf crash_jmp;
static status_$t crash_status;

ml_$spin_token_t ML_$SPIN_LOCK(void *p) { (void)p; spin_locks++; return 1; }
void ML_$SPIN_UNLOCK(void *p, ml_$spin_token_t t) { (void)p; (void)t; spin_unlocks++; }
void EC_$ADVANCE(ec_$eventcount_t *ec) { (void)ec; advances++; }
void CRASH_SYSTEM(const status_$t *s) { crash_status = *s; longjmp(crash_jmp, 1); }
void DISK_$WRITE(int16_t vol_idx, uint32_t daddr, uint32_t ppn, uint32_t *info,
                 status_$t *status)
{
    (void)daddr; (void)ppn;
    writes++; write_vol = vol_idx;
    memcpy(write_hdr, info, sizeof write_hdr);
    *status = write_status;
}

#include "../set_buff.c"

#define N 3
#define DATA_VA(i) (0xD50000u + (i) * 0x400u)
static uint32_t va(const void *p) { return ARCH_PTR_TO_VA(p); }

static void reset(void)
{
    int i;
    ARCH_HOST_VA_BASE = (uintptr_t)DBUF - 0x10000;
    memset(DBUF, 0, sizeof DBUF);
    for (i = 0; i < N; i++) {
        DBUF[i].next = (i + 1 < N) ? va(&DBUF[i + 1]) : 0;
        DBUF[i].prev = (i > 0) ? va(&DBUF[i - 1]) : 0;
        DBUF[i].data = DATA_VA(i);
        DBUF[i].block = 0x10 + i;
        DBUF[i].flags = 4;          /* volume 4 */
        DBUF[i].ref_count = 1;
    }
    dbuf_$head = va(&DBUF[0]);
    dbuf_$count = N;
    dbuf_$waiters = 0;
    DBUF_$TROUBLE = 0;
    spin_locks = spin_unlocks = advances = writes = 0;
    write_status = 0;
}

static void *buf(int i) { return ARCH_VA_TO_PTR(DATA_VA(i)); }

TEST(release_decrements)
{
    status_$t st = 0x99;
    reset();
    DBUF_$SET_BUFF(buf(1), DBUF_RELEASE_CLEAN, &st);
    ASSERT_EQ(0, st);
    ASSERT_EQ(0, DBUF[1].ref_count);
    ASSERT_EQ(0, advances);
    ASSERT_EQ(1, spin_locks);
    ASSERT_EQ(1, spin_unlocks);
}

TEST(release_to_zero_wakes_waiters)
{
    status_$t st = 0;
    reset();
    dbuf_$waiters = 2;
    DBUF_$SET_BUFF(buf(2), DBUF_RELEASE_CLEAN, &st);
    ASSERT_EQ(1, advances);
}

TEST(dirty_then_writeback)
{
    status_$t st = 0;
    reset();
    DBUF[0].uid.high = 5; DBUF[0].uid.low = 6; DBUF[0].hint = 7; DBUF[0].type = 0x33;
    DBUF_$SET_BUFF(buf(0), DBUF_RELEASE_WRITEBACK, &st);
    ASSERT_EQ(1, writes);
    ASSERT_EQ(4, write_vol);
    ASSERT_EQ(5, write_hdr[0]);
    ASSERT_EQ(7, write_hdr[2]);
    ASSERT_EQ(0x33000000, write_hdr[4]);
    ASSERT_EQ(4, DBUF[0].flags);            /* dirty cleared */
    ASSERT_EQ(0, DBUF[0].ref_count);
}

TEST(writeback_skipped_when_clean)
{
    status_$t st = 0;
    reset();
    DBUF_$SET_BUFF(buf(0), DBUF_FLAG_WRITEBACK, &st);
    ASSERT_EQ(0, writes);
    ASSERT_EQ(1, DBUF[0].ref_count);
}

TEST(write_error_reported_and_trouble_set)
{
    status_$t st = 0;
    reset();
    DBUF[0].flags |= DBUF_ENTRY_DIRTY;
    write_status = 0x80009;
    DBUF_$SET_BUFF(buf(0), DBUF_FLAG_WRITEBACK, &st);
    ASSERT_EQ(0x80009, st);
    ASSERT_EQ(1u << 4, DBUF_$TROUBLE);
}

TEST(invalidate_flag_empties_entry)
{
    status_$t st = 0;
    reset();
    DBUF[1].flags = DBUF_ENTRY_DIRTY | 4;
    DBUF_$SET_BUFF(buf(1), DBUF_FLAG_INVALIDATE, &st);
    ASSERT_EQ(0, DBUF[1].flags);
    ASSERT_EQ(-1, DBUF[1].block);
    ASSERT_EQ(1, DBUF[1].ref_count);
}

TEST(bad_pointer_crashes_after_unlock)
{
    status_$t st = 0;
    reset();
    if (setjmp(crash_jmp) == 0) {
        DBUF_$SET_BUFF(ARCH_VA_TO_PTR(0x12345u), DBUF_RELEASE_CLEAN, &st);
        ASSERT_EQ(1, 0);
    }
    ASSERT_EQ(0x000C0001, crash_status);
    ASSERT_EQ(1, spin_unlocks);
}

TEST(bad_free_crashes)
{
    status_$t st = 0;
    reset();
    DBUF[2].ref_count = 0;
    if (setjmp(crash_jmp) == 0) {
        DBUF_$SET_BUFF(buf(2), DBUF_RELEASE_CLEAN, &st);
        ASSERT_EQ(1, 0);
    }
    ASSERT_EQ(0x000C0002, crash_status);
}

int main(void)
{
    printf("test_set_buff:\n");
    RUN_TEST(release_decrements);
    RUN_TEST(release_to_zero_wakes_waiters);
    RUN_TEST(dirty_then_writeback);
    RUN_TEST(writeback_skipped_when_clean);
    RUN_TEST(write_error_reported_and_trouble_set);
    RUN_TEST(invalidate_flag_empties_entry);
    RUN_TEST(bad_pointer_crashes_after_unlock);
    RUN_TEST(bad_free_crashes);
    printf("%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
