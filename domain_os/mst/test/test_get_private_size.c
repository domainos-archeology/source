/*
 * mst/test/test_get_private_size.c - unit tests for MST_$GET_PRIVATE_SIZE (0x00E44AAE).
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int tests_passed = 0;
static int tests_failed = 0;
static int current_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do {                     \
    printf("  Running %s... ", #name);          \
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

#include "mst/mst_internal.h"

/* MST_PAGE_TABLE_BASE is ARCH_PTR_TO_VA(MSTE_PAGES), a sau2.ld symbol past
 * OS_PAGE_END since source-o7s2 (docs/rfc-cold-start.md section 8c); this
 * test stands in for the link with the map's value, `EF6400 MSTE_PAGES'
 * (os/test/test_vm_tables.c checks the macro itself). */
#undef MST_PAGE_TABLE_BASE
#define MST_PAGE_TABLE_BASE 0x00EF6400u
#include "ml/ml.h"

uint16_t MST[MST_TABLE_ENTRIES];
uint16_t MST_ASID_BASE[MST_MAX_ASIDS];

/* Page-table page 1 is VA 0xEF6400 = pte_pages[0]. */
static uint8_t pte_pages[4 * 0x400] __attribute__((aligned(0x400)));
static int n_lock, n_unlock;

void ML_$LOCK(int16_t id) { if (id == MST_LOCK_ASID) n_lock++; }
void ML_$UNLOCK(int16_t id) { if (id == MST_LOCK_ASID) n_unlock++; }

#include "../get_private_size.c"

static mste_t *entry(int page, int i)
{
    return (mste_t *)(void *)&pte_pages[(page - 1) * 0x400 + i * 0x10];
}

static void reset(void)
{
    memset(MST, 0, sizeof(MST));
    memset(pte_pages, 0, sizeof(pte_pages));
    n_lock = n_unlock = 0;
    ARCH_HOST_VA_BASE = (uintptr_t)pte_pages - (uintptr_t)MST_PAGE_TABLE_BASE;
}

TEST(bad_asid)
{
    uint16_t asid = 0x3A;
    uint32_t a = 7, b = 7;
    status_$t st = 0;
    reset();
    MST_$GET_PRIVATE_SIZE(&asid, &a, &b, &st);
    ASSERT_EQ(status_$reference_to_illegal_address, st);
    ASSERT_EQ(7, a);
    ASSERT_EQ(0, n_lock);
}

TEST(counts)
{
    uint16_t asid = 2;
    uint32_t a, b;
    status_$t st = 1;

    reset();
    MST_ASID_BASE[2] = 0x10;
    MST[0x10] = 1;          /* page 1 */
    MST[0x12] = 3;          /* page 3; slots 0x11 and 0x13 empty */
    MST[0x14] = 2;          /* past the four slots: not scanned */
    entry(1, 0)->uid.high = 1;  entry(1, 0)->unknown_0a = 0x0600;   /* prot 3 */
    entry(1, 63)->uid.high = 1; entry(1, 63)->unknown_0a = 0x0400;  /* prot 2 */
    entry(1, 5)->uid.high = 1;  entry(1, 5)->unknown_0a = 0x4600;   /* bit 14: skip */
    entry(1, 6)->uid.low = 1;   entry(1, 6)->unknown_0a = 0x0600;   /* high 0: skip */
    entry(3, 2)->uid.high = 9;  entry(3, 2)->unknown_0a = 0x4600 & 0xBFFF;
    entry(2, 0)->uid.high = 1;
    MST_$GET_PRIVATE_SIZE(&asid, &a, &b, &st);
    ASSERT_EQ(status_$ok, st);
    ASSERT_EQ(3 * 32, a);
    ASSERT_EQ(2 * 32, b);
    ASSERT_EQ(1, n_lock);
    ASSERT_EQ(1, n_unlock);
}

int main(void)
{
    printf("MST_$GET_PRIVATE_SIZE tests\n");
    RUN_TEST(bad_asid);
    RUN_TEST(counts);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
