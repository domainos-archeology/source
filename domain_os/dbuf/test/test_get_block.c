/*
 * dbuf/test/test_get_block.c - Unit tests for DBUF_$GET_BLOCK (0x00E3A5B0)
 *
 * dbuf/get_block.c is #included below with DISK_$READ / DISK_$WRITE, the
 * spin lock, EC_$WAIT / EC_$ADVANCE and NETLOG mocked.  The LRU links are
 * 32-bit VAs, so ARCH_HOST_VA_BASE is pointed below the DBUF array and
 * the buffer "data" VAs are plain numbers the code never dereferences.
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

/* module data */
ec_$eventcount_t dbuf_$eventcount;
dbuf_$entry_t DBUF[DBUF_MAX_BUFFERS];
uint32_t DBUF_SPIN_LOCK;
uint32_t dbuf_$head;
uint16_t dbuf_$waiters;
uint16_t dbuf_$count;
uint16_t DBUF_$TROUBLE;
int8_t NETLOG_$OK_TO_LOG;

static int spin_locks, spin_unlocks, advances, waits, reads, writes;
static int16_t read_vol, write_vol;
static uint32_t read_daddr, read_ppn, write_daddr, write_ppn;
static uint32_t read_hdr[8], write_hdr[8];
static status_$t read_status, write_status;
static int wait_sets_free;      /* EC_$WAIT mock frees entry 0 */

ml_$spin_token_t ML_$SPIN_LOCK(void *p) { (void)p; spin_locks++; return 0x2100; }
void ML_$SPIN_UNLOCK(void *p, ml_$spin_token_t t) { (void)p; (void)t; spin_unlocks++; }
void EC_$ADVANCE(ec_$eventcount_t *ec) { advances++; ec->value++; }
int16_t EC_$WAIT(ec_$wait_ecs_t ecs, ec_$wait_vals_t vals)
{
    (void)ecs; (void)vals;
    waits++;
    if (wait_sets_free) {
        DBUF[0].ref_count = 0;
        DBUF[0].flags &= (uint8_t)~DBUF_ENTRY_BUSY;
    }
    return 0;
}
void NETLOG_$LOG_IT(uint16_t k, uint32_t *u, uint16_t a, uint16_t b, uint16_t c,
                    uint16_t d, uint16_t e, uint16_t f)
{ (void)k; (void)u; (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; }

void DISK_$READ(int16_t vol_idx, uint32_t daddr, uint32_t ppn, uint32_t *info,
                status_$t *status)
{
    reads++;
    read_vol = vol_idx; read_daddr = daddr; read_ppn = ppn;
    memcpy(read_hdr, info, sizeof read_hdr);
    *status = read_status;
}

void DISK_$WRITE(int16_t vol_idx, uint32_t daddr, uint32_t ppn, uint32_t *info,
                 status_$t *status)
{
    writes++;
    write_vol = vol_idx; write_daddr = daddr; write_ppn = ppn;
    memcpy(write_hdr, info, sizeof write_hdr);
    *status = write_status;
}

#include "../get_block.c"

#define N 4
#define DATA_VA(i) (0xD50000u + (i) * 0x400u)

static uint32_t va(const void *p) { return ARCH_PTR_TO_VA(p); }

/* head = DBUF[0], tail = DBUF[N-1]; every entry empty */
static void reset(void)
{
    int i;
    ARCH_HOST_VA_BASE = (uintptr_t)DBUF - 0x10000;
    memset(DBUF, 0, sizeof DBUF);
    for (i = 0; i < N; i++) {
        DBUF[i].next = (i + 1 < N) ? va(&DBUF[i + 1]) : 0;
        DBUF[i].prev = (i > 0) ? va(&DBUF[i - 1]) : 0;
        DBUF[i].data = DATA_VA(i);
        DBUF[i].ppn = 0x100 + i;
        DBUF[i].block = -1;
    }
    dbuf_$head = va(&DBUF[0]);
    dbuf_$count = N;
    dbuf_$waiters = 0;
    DBUF_$TROUBLE = 0;
    dbuf_$eventcount.value = 10;
    NETLOG_$OK_TO_LOG = 0;
    spin_locks = spin_unlocks = advances = waits = reads = writes = 0;
    read_status = write_status = 0;
    wait_sets_free = 0;
}

/* order of the LRU list from the head, as entry indexes */
static int order(int out[N])
{
    int n = 0;
    uint32_t p = dbuf_$head;
    while (p != 0 && n < N) {
        out[n++] = (int)(dbuf_$entry_ptr(p) - DBUF);
        p = dbuf_$entry_ptr(p)->next;
    }
    return n;
}

TEST(miss_reads_lru_tail_and_returns_it)
{
    uid_t u = { 0x11, 0x22 };
    status_$t st = 0x55;
    int o[N];
    void *r;
    reset();
    r = DBUF_$GET_BLOCK(3, 0x1234, &u, 0x77, 9, 0, &st);
    ASSERT_EQ(0, st);
    /* the tail (entry 3) was taken, read, and is now the head */
    ASSERT_EQ(DATA_VA(3), (unsigned long)ARCH_PTR_TO_VA(r));
    ASSERT_EQ(1, reads);
    ASSERT_EQ(3, read_vol);
    ASSERT_EQ(0x1234, read_daddr);
    ASSERT_EQ(0x103, read_ppn);
    ASSERT_EQ(0x11, read_hdr[0]);
    ASSERT_EQ(0x22, read_hdr[1]);
    ASSERT_EQ(0x77, read_hdr[2]);
    ASSERT_EQ(4, order(o));
    ASSERT_EQ(3, o[0]);
    ASSERT_EQ(0, o[1]);
    ASSERT_EQ(1, DBUF[3].ref_count);
    ASSERT_EQ(3, DBUF[3].flags);            /* vol 3, not busy, not dirty */
    ASSERT_EQ(9, DBUF[3].type);
    ASSERT_EQ(0x1234, DBUF[3].block);
    ASSERT_EQ(0x77, DBUF[3].hint);
    /* lock, unlock (claim), settle-lock, then the hit path unlocks */
    ASSERT_EQ(2, spin_locks);
    ASSERT_EQ(2, spin_unlocks);
    ASSERT_EQ(0, advances);
}

TEST(hit_bumps_refcount_and_moves_to_head)
{
    uid_t u = { 1, 2 };
    status_$t st = 0;
    int o[N];
    void *r;
    reset();
    DBUF[2].block = 0x50; DBUF[2].flags = 5; DBUF[2].ref_count = 1;
    r = DBUF_$GET_BLOCK(5, 0x50, &u, 0, 0, 0, &st);
    ASSERT_EQ(DATA_VA(2), (unsigned long)ARCH_PTR_TO_VA(r));
    ASSERT_EQ(0, reads);
    ASSERT_EQ(2, DBUF[2].ref_count);
    order(o);
    ASSERT_EQ(2, o[0]);
    ASSERT_EQ(0, o[1]);
    ASSERT_EQ(1, o[2]);
    ASSERT_EQ(3, o[3]);
    ASSERT_EQ(0, DBUF[3].next);
    ASSERT_EQ(0, DBUF[2].prev);
}

TEST(hit_with_no_read_refreshes_entry)
{
    uid_t u = { 0xAA, 0xBB };
    status_$t st = 0;
    reset();
    DBUF[0].block = 7; DBUF[0].flags = 1;
    (void)DBUF_$GET_BLOCK(1, 7, &u, 0x99, 0x1234, DBUF_GET_NO_READ, &st);
    ASSERT_EQ(0xAA, DBUF[0].uid.high);
    ASSERT_EQ(0x99, DBUF[0].hint);
    ASSERT_EQ(0x34, DBUF[0].type);
}

TEST(dirty_victim_written_back_first)
{
    uid_t u = { 1, 2 };
    status_$t st = 0;
    void *r;
    reset();
    /* tail is dirty for volume 2, block 0x40 */
    DBUF[3].block = 0x40; DBUF[3].flags = DBUF_ENTRY_DIRTY | 2; DBUF[3].type = 0x42;
    DBUF[3].uid.high = 0xD1; DBUF[3].uid.low = 0xD2; DBUF[3].hint = 0xD3;
    r = DBUF_$GET_BLOCK(1, 0x1000, &u, 0, 0, 0, &st);
    ASSERT_EQ(1, writes);
    ASSERT_EQ(2, write_vol);
    ASSERT_EQ(0x40, write_daddr);
    ASSERT_EQ(0xD1, write_hdr[0]);
    ASSERT_EQ(0xD2, write_hdr[1]);
    ASSERT_EQ(0xD3, write_hdr[2]);
    ASSERT_EQ(0x42000000, write_hdr[4]);
    /* after the writeback the search ran again and the same (now clean)
     * tail was claimed and read */
    ASSERT_EQ(1, reads);
    ASSERT_EQ(DATA_VA(3), (unsigned long)ARCH_PTR_TO_VA(r));
    ASSERT_EQ(0, st);
    ASSERT_EQ(0, DBUF_$TROUBLE);
}

TEST(writeback_failure_sets_trouble_and_continues)
{
    uid_t u = { 1, 2 };
    status_$t st = 0;
    reset();
    DBUF[3].block = 0x40; DBUF[3].flags = DBUF_ENTRY_DIRTY | 2;
    write_status = 0x80001;
    (void)DBUF_$GET_BLOCK(1, 0x1000, &u, 0, 0, 0, &st);
    ASSERT_EQ(1u << 2, DBUF_$TROUBLE);
    ASSERT_EQ(0, st);
    ASSERT_EQ(1, reads);
}

TEST(read_failure_returns_null_with_bit31)
{
    uid_t u = { 1, 2 };
    status_$t st = 0;
    void *r;
    reset();
    read_status = 0x00080005;
    r = DBUF_$GET_BLOCK(1, 0x1000, &u, 0, 0, 0, &st);
    ASSERT_EQ(0, (unsigned long)r);
    ASSERT_EQ((status_$t)0x80080005, st);
    ASSERT_EQ(-1, DBUF[3].block);
    ASSERT_EQ(0, DBUF[3].flags);
    ASSERT_EQ(0, DBUF[3].ref_count);
}

TEST(stopped_status_tolerated_when_asked)
{
    uid_t u = { 1, 2 };
    status_$t st = 0;
    void *r;
    reset();
    read_status = status_$storage_module_stopped;
    r = DBUF_$GET_BLOCK(1, 0x1000, &u, 0, 0, DBUF_GET_OK_IF_STOPPED, &st);
    ASSERT_EQ(DATA_VA(3), (unsigned long)ARCH_PTR_TO_VA(r));
    ASSERT_EQ(status_$storage_module_stopped, st);
    ASSERT_EQ(1, DBUF[3].ref_count);
}

TEST(all_busy_waits_then_retries)
{
    uid_t u = { 1, 2 };
    status_$t st = 0;
    int i;
    void *r;
    reset();
    for (i = 0; i < N; i++) {
        DBUF[i].ref_count = 1;
    }
    wait_sets_free = 1;
    r = DBUF_$GET_BLOCK(1, 0x1000, &u, 0, 0, 0, &st);
    ASSERT_EQ(1, waits);
    ASSERT_EQ(0, dbuf_$waiters);
    ASSERT_EQ(DATA_VA(0), (unsigned long)ARCH_PTR_TO_VA(r));
    ASSERT_EQ(1, reads);
}

TEST(waiters_are_advanced_after_read)
{
    uid_t u = { 1, 2 };
    status_$t st = 0;
    reset();
    dbuf_$waiters = 1;
    (void)DBUF_$GET_BLOCK(1, 0x1000, &u, 0, 0, 0, &st);
    ASSERT_EQ(1, advances);
    /* lock, unlock, settle-lock, unlock, retry-lock, hit-unlock */
    ASSERT_EQ(3, spin_locks);
    ASSERT_EQ(3, spin_unlocks);
}

int main(void)
{
    printf("test_get_block:\n");
    RUN_TEST(miss_reads_lru_tail_and_returns_it);
    RUN_TEST(hit_bumps_refcount_and_moves_to_head);
    RUN_TEST(hit_with_no_read_refreshes_entry);
    RUN_TEST(dirty_victim_written_back_first);
    RUN_TEST(writeback_failure_sets_trouble_and_continues);
    RUN_TEST(read_failure_returns_null_with_bit31);
    RUN_TEST(stopped_status_tolerated_when_asked);
    RUN_TEST(all_busy_waits_then_retries);
    RUN_TEST(waiters_are_advanced_after_read);
    printf("%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
