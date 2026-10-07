/*
 * dbuf/test/test_init.c - Unit tests for DBUF_$INIT (0x00E3ABDA)
 */

#include <stdio.h>
#include <string.h>
#include <setjmp.h>

#include "dbuf/dbuf_internal.h"
#include "uid/uid.h"

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
MODULE_DATA_DEFINE(mmap_globals_t, MMAP_$DATA, 0x00E23284);   /* MMAP_$REAL_PAGES lives in it */
uid_t UID_$NIL = { 0x0A, 0x0B };

static int callocs, installs, inhibits, ec_inits;
static uint32_t install_va[64], install_ppn[64], install_flags[64], inhibit_va[64];
static status_$t calloc_status;
static jmp_buf crash_jmp;
static status_$t crash_status;

void WP_$CALLOC(uint32_t *ppn_out, status_$t *status)
{
    *ppn_out = 0x500 + (uint32_t)callocs;
    callocs++;
    *status = calloc_status;
}
void (MMU_$INSTALL)(uint32_t ppn, uint32_t va, uint32_t flags)
{
    if (installs < 64) { install_ppn[installs] = ppn; install_va[installs] = va; install_flags[installs] = flags; }
    installs++;
}
void MMU_$CACHE_INHIBIT_VA(uint32_t va) { if (inhibits < 64) inhibit_va[inhibits] = va; inhibits++; }
void EC_$INIT(ec_$eventcount_t *ec) { (void)ec; ec_inits++; }
void CRASH_SYSTEM(const status_$t *s) { crash_status = *s; longjmp(crash_jmp, 1); }

#include "../init.c"

static void reset(uint32_t real_pages)
{
    ARCH_HOST_VA_BASE = (uintptr_t)DBUF - 0x10000;
    memset(DBUF, 0xEE, sizeof DBUF);
    MMAP_$REAL_PAGES = real_pages;
    callocs = installs = inhibits = ec_inits = 0;
    calloc_status = 0;
    dbuf_$waiters = 7; DBUF_$TROUBLE = 7;
}

TEST(count_from_real_pages)
{
    reset(0x800);       /* (0x800 >> 10) << 4 = 32 */
    DBUF_$INIT();
    ASSERT_EQ(32, dbuf_$count);
    ASSERT_EQ(32, callocs);
    ASSERT_EQ(32, installs);
    ASSERT_EQ(32, inhibits);
    ASSERT_EQ(1, ec_inits);
    ASSERT_EQ(0, dbuf_$waiters);
    ASSERT_EQ(0, DBUF_$TROUBLE);
}

TEST(clamped_to_minimum)
{
    reset(0x100);
    DBUF_$INIT();
    ASSERT_EQ(6, dbuf_$count);
}

TEST(clamped_to_maximum)
{
    reset(0x100000);
    DBUF_$INIT();
    ASSERT_EQ(64, dbuf_$count);
}

TEST(entries_linked_and_mapped_from_d50000)
{
    reset(0x100);
    DBUF_$INIT();
    ASSERT_EQ(ARCH_PTR_TO_VA(&DBUF[0]), dbuf_$head);
    ASSERT_EQ(0, DBUF[0].prev);
    ASSERT_EQ(ARCH_PTR_TO_VA(&DBUF[1]), DBUF[0].next);
    ASSERT_EQ(ARCH_PTR_TO_VA(&DBUF[4]), DBUF[5].prev);
    ASSERT_EQ(0, DBUF[5].next);
    ASSERT_EQ(0xD50000, DBUF[0].data);
    ASSERT_EQ(0xD51400, DBUF[5].data);
    ASSERT_EQ(0xD50000, install_va[0]);
    ASSERT_EQ(0xD50400, install_va[1]);
    ASSERT_EQ(0x16, install_flags[0]);
    ASSERT_EQ(0x500, install_ppn[0]);
    ASSERT_EQ(0xD51400, inhibit_va[5]);
    ASSERT_EQ(0x505, DBUF[5].ppn);
    ASSERT_EQ(-1, DBUF[3].block);
    ASSERT_EQ(0, DBUF[3].flags);
    ASSERT_EQ(0, DBUF[3].type);
    ASSERT_EQ(0, DBUF[3].ref_count);
    ASSERT_EQ(0x0A, DBUF[3].uid.high);
    ASSERT_EQ(0x0B, DBUF[3].uid.low);
    ASSERT_EQ(0, DBUF[3].hint);
    /* entry 6 is beyond the pool and untouched */
    ASSERT_EQ(0xEEEEEEEE, DBUF[6].next);
}

TEST(calloc_failure_crashes_with_status)
{
    reset(0x100);
    calloc_status = 0x40003;
    if (setjmp(crash_jmp) == 0) {
        DBUF_$INIT();
        ASSERT_EQ(1, 0);
    }
    ASSERT_EQ(0x40003, crash_status);
    ASSERT_EQ(0, installs);
}

int main(void)
{
    printf("test_init:\n");
    RUN_TEST(count_from_real_pages);
    RUN_TEST(clamped_to_minimum);
    RUN_TEST(clamped_to_maximum);
    RUN_TEST(entries_linked_and_mapped_from_d50000);
    RUN_TEST(calloc_failure_crashes_with_status);
    printf("%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
