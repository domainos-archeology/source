/*
 * mst/test/test_set_mstes.c - unit tests for mst_$set_mstes (0x00E43E10).
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
#include "cache/cache.h"

uint16_t MST[MST_TABLE_ENTRIES];
uint16_t MST_ASID_BASE[MST_MAX_ASIDS];
static uint8_t pte_pages[4 * 0x400] __attribute__((aligned(0x400)));
static int n_clear;

uint32_t CACHE_$CLEAR(void) { n_clear++; return 0; }

#include "../set_mstes.c"

static mste_t *entry(int page, int i)
{
    return (mste_t *)(void *)&pte_pages[(page - 1) * 0x400 + i * 0x10];
}

static void reset(void)
{
    memset(MST, 0, sizeof(MST));
    memset(pte_pages, 0xEE, sizeof(pte_pages));
    n_clear = 0;
    ARCH_HOST_VA_BASE = (uintptr_t)pte_pages - (uintptr_t)MST_PAGE_TABLE_BASE;
}

TEST(single_entry_fields)
{
    uid_t u = { 0x11223344u, 0x55667788u };

    reset();
    MST_ASID_BASE[3] = 0x20;
    MST[0x20] = 2;
    mst_$set_mstes(&u, 7, 0x12345678u, 5, 5, 0x0104, 3, 0x0011, (int8_t)-1);
    ASSERT_EQ(0x11223344u, entry(2, 5)->uid.high);
    ASSERT_EQ(7, entry(2, 5)->segment);
    /* 0xEE & 0x01 = 0; | (0x13 << 1) = 0x26; & 0xFE00; bit 15 */
    ASSERT_EQ(0xA600, entry(2, 5)->unknown_0a);
    /* 0x12 & 0x83 = 0x02; | ((4 - 1) << 2) = 0x0E */
    ASSERT_EQ(0x0E345678u, entry(2, 5)->location);
    ASSERT_EQ(0xEEEEEEEEu, entry(2, 6)->uid.high);
    ASSERT_EQ(1, n_clear);

    reset();
    MST[0x20] = 2;
    memset(entry(2, 5), 0xFF, 16);
    mst_$set_mstes(&u, 7, 0x00000000u, 5, 5, 0x0000, 3, 0x0000, 0);
    /* bit 8 (byte bit 0) survives 0x3F/0xC1 but not 0xFE00; touch 0-1 */
    ASSERT_EQ(0x0400, entry(2, 5)->unknown_0a);
    ASSERT_EQ(0x7C000000u, entry(2, 5)->location);
}

/* Segments 0x3E..0x41 span two page-table pages. */
TEST(copies_across_pages)
{
    uid_t u = { 1, 2 };
    int i;

    reset();
    MST_ASID_BASE[0] = 0x10;
    MST[0x10] = 1;
    MST[0x11] = 3;
    mst_$set_mstes(&u, 0x100, 0x04000000u, 0x3E, 0x41, 1, 0, 0, 0);
    ASSERT_EQ(0x100, entry(1, 0x3E)->segment);
    ASSERT_EQ(0x101, entry(1, 0x3F)->segment);
    ASSERT_EQ(0x102, entry(3, 0)->segment);
    ASSERT_EQ(0x103, entry(3, 1)->segment);
    for (i = 0; i < 2; i++) {
        ASSERT_EQ(1, entry(3, i)->uid.high);
        ASSERT_EQ(entry(1, 0x3E)->unknown_0a, entry(3, i)->unknown_0a);
        ASSERT_EQ(entry(1, 0x3E)->location, entry(3, i)->location);
    }
    ASSERT_EQ(0xEEEEEEEEu, entry(3, 2)->uid.high);
    ASSERT_EQ(0xEEEEEEEEu, entry(1, 0x3D)->uid.high);
}

int main(void)
{
    printf("mst_$set_mstes tests\n");
    RUN_TEST(single_entry_fields);
    RUN_TEST(copies_across_pages);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
