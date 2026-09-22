/*
 * dbuf/test/test_invalidate.c - Unit tests for DBUF_$INVALIDATE (0x00E3A9EC)
 */

#include <stdio.h>
#include <string.h>

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

static int spin_locks, spin_unlocks, advances;
ml_$spin_token_t ML_$SPIN_LOCK(void *p) { (void)p; spin_locks++; return 1; }
void ML_$SPIN_UNLOCK(void *p, ml_$spin_token_t t) { (void)p; (void)t; spin_unlocks++; }
void EC_$ADVANCE(ec_$eventcount_t *ec) { (void)ec; advances++; }

#include "../invalidate.c"

static void reset(void)
{
    int i;
    memset(DBUF, 0, sizeof DBUF);
    for (i = 0; i < 4; i++) {
        DBUF[i].block = 0x10 + i;
        DBUF[i].flags = DBUF_ENTRY_DIRTY | DBUF_ENTRY_BUSY | 2;   /* volume 2 */
        DBUF[i].ref_count = 3;
    }
    DBUF[1].flags = DBUF_ENTRY_DIRTY | 3;                       /* volume 3 */
    dbuf_$count = 4;
    dbuf_$waiters = 0;
    DBUF_$TROUBLE = 0xFFFF;
    spin_locks = spin_unlocks = advances = 0;
}

TEST(whole_volume)
{
    reset();
    DBUF_$INVALIDATE(0, 2);
    ASSERT_EQ(0, DBUF[0].flags);
    ASSERT_EQ(-1, DBUF[0].block);
    ASSERT_EQ(0, DBUF[0].ref_count);
    ASSERT_EQ(0, DBUF[2].flags);
    ASSERT_EQ(0, DBUF[3].flags);
    ASSERT_EQ(DBUF_ENTRY_DIRTY | 3, DBUF[1].flags);     /* other volume kept */
    ASSERT_EQ(0x11, DBUF[1].block);
    ASSERT_EQ(3, spin_locks);
    ASSERT_EQ(3, spin_unlocks);
    ASSERT_EQ(0, advances);
    ASSERT_EQ(0xFFFF & ~(1u << 2), DBUF_$TROUBLE);
}

TEST(single_block_stops_at_first_match)
{
    reset();
    DBUF[3].block = 0x12;                       /* duplicate of entry 2 */
    dbuf_$waiters = 1;
    DBUF_$INVALIDATE(0x12, 2);
    ASSERT_EQ(-1, DBUF[2].block);
    ASSERT_EQ(0x12, DBUF[3].block);             /* not reached */
    ASSERT_EQ(1, advances);
    ASSERT_EQ(1, spin_locks);
}

TEST(no_match_only_clears_trouble)
{
    reset();
    DBUF_$INVALIDATE(0x99, 2);
    ASSERT_EQ(0x10, DBUF[0].block);
    ASSERT_EQ(0, spin_locks);
    ASSERT_EQ(0xFFFF & ~(1u << 2), DBUF_$TROUBLE);
}

/* A volume index of 16 or more selects a bit outside the trouble word. */
TEST(volume_16_clears_nothing)
{
    reset();
    DBUF_$INVALIDATE(0, 16);
    ASSERT_EQ(0xFFFF, DBUF_$TROUBLE);
}

int main(void)
{
    printf("test_invalidate:\n");
    RUN_TEST(whole_volume);
    RUN_TEST(single_block_stops_at_first_match);
    RUN_TEST(no_match_only_clears_trouble);
    RUN_TEST(volume_16_clears_nothing);
    printf("%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
