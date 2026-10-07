/*
 * dbuf/test/test_update_vol.c - Unit tests for DBUF_$UPDATE_VOL (0x00E3AAA2)
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

static int spin_locks, spin_unlocks, advances, writes;
static int16_t write_vols[8];
static uint32_t write_hdr[8];
static status_$t write_status;
static int busy_during_write;

ml_$spin_token_t ML_$SPIN_LOCK(void *p) { (void)p; spin_locks++; return 1; }
void (ML_$SPIN_UNLOCK)(void *p, uint32_t t_slot) { ml_$spin_token_t t = (ml_$spin_token_t)ARCH_PASCAL_SLOT_WORD(t_slot); (void)t; (void)p; (void)t; spin_unlocks++; }
void EC_$ADVANCE(ec_$eventcount_t *ec) { (void)ec; advances++; }
void DISK_$WRITE(int16_t vol_idx, uint32_t daddr, uint32_t ppn, uint32_t *info,
                 status_$t *status)
{
    int i;
    (void)daddr; (void)ppn;
    if (writes < 8) write_vols[writes] = vol_idx;
    writes++;
    memcpy(write_hdr, info, sizeof write_hdr);
    /* the entry being written must be marked busy meanwhile */
    for (i = 0; i < 4; i++) {
        if ((DBUF[i].flags & DBUF_ENTRY_BUSY) != 0) busy_during_write++;
    }
    *status = write_status;
}

#include "../update_vol.c"

static void reset(void)
{
    int i;
    memset(DBUF, 0, sizeof DBUF);
    for (i = 0; i < 4; i++) {
        DBUF[i].block = 0x20 + i;
        DBUF[i].flags = DBUF_ENTRY_DIRTY | 1;       /* dirty, volume 1 */
        DBUF[i].type = (uint8_t)(0x10 + i);
        DBUF[i].hint = (uint32_t)i;
    }
    DBUF[2].flags = DBUF_ENTRY_DIRTY | 2;           /* volume 2 */
    dbuf_$count = 4;
    dbuf_$waiters = 0;
    DBUF_$TROUBLE = 0;
    spin_locks = spin_unlocks = advances = writes = busy_during_write = 0;
    write_status = 0;
}

TEST(flushes_one_volume)
{
    reset();
    DBUF_$UPDATE_VOL(1, NULL);
    ASSERT_EQ(3, writes);
    ASSERT_EQ(1, write_vols[0]);
    ASSERT_EQ(1, write_vols[2]);
    ASSERT_EQ(3, busy_during_write);
    ASSERT_EQ(1, DBUF[0].flags);                    /* clean, not busy */
    ASSERT_EQ(DBUF_ENTRY_DIRTY | 2, DBUF[2].flags); /* other volume untouched */
    ASSERT_EQ(0x13000000, write_hdr[4]);            /* type of the last one */
    ASSERT_EQ(6, spin_locks);
    ASSERT_EQ(6, spin_unlocks);
    ASSERT_EQ(0, advances);
}

TEST(volume_zero_flushes_all)
{
    reset();
    DBUF_$UPDATE_VOL(0, NULL);
    ASSERT_EQ(4, writes);
    ASSERT_EQ(2, write_vols[2]);
}

TEST(referenced_and_busy_entries_skipped)
{
    reset();
    DBUF[0].ref_count = 1;
    DBUF[1].flags |= DBUF_ENTRY_BUSY;
    DBUF_$UPDATE_VOL(1, NULL);
    ASSERT_EQ(1, writes);
    ASSERT_EQ(DBUF_ENTRY_DIRTY | 1, DBUF[0].flags);
    ASSERT_EQ(DBUF_ENTRY_DIRTY | DBUF_ENTRY_BUSY | 1, DBUF[1].flags);
    /* the two skipped ones still took and released the lock */
    ASSERT_EQ(4, spin_locks);
}

TEST(clean_entries_never_locked)
{
    reset();
    DBUF[0].flags = 1; DBUF[1].flags = 1; DBUF[3].flags = 1;
    DBUF_$UPDATE_VOL(1, NULL);
    ASSERT_EQ(0, writes);
    ASSERT_EQ(0, spin_locks);
}

TEST(write_error_sets_trouble_and_waiters_woken)
{
    reset();
    write_status = 0x80009;
    dbuf_$waiters = 1;
    DBUF_$UPDATE_VOL(2, NULL);
    ASSERT_EQ(1, writes);
    ASSERT_EQ(1u << 2, DBUF_$TROUBLE);
    ASSERT_EQ(1, advances);
}

int main(void)
{
    printf("test_update_vol:\n");
    RUN_TEST(flushes_one_volume);
    RUN_TEST(volume_zero_flushes_all);
    RUN_TEST(referenced_and_busy_entries_skipped);
    RUN_TEST(clean_entries_never_locked);
    RUN_TEST(write_error_sets_trouble_and_waiters_woken);
    printf("%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
