/*
 * mst/test/test_map_canned_at.c - unit tests for MST_$MAP_CANNED_AT (0x00E30FAA).
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

uint16_t MST[MST_TABLE_ENTRIES];
uint16_t MST_ASID_BASE[MST_MAX_ASIDS];
static uint8_t pte_pages[4 * 0x400] __attribute__((aligned(0x400)));

static uint16_t segno_ret, seg_ret, segno_asid;
static int n_set;
static uid_t *s_uid;
static uint16_t s_obj, s_start, s_end, s_touch, s_asid, s_prot;
static uint32_t s_loc;
static int8_t s_wired;

uint16_t MST_$VA_TO_SEGNO(uint32_t va, uint16_t *seg_out, uint16_t asid)
{
    (void)va; segno_asid = asid;
    *seg_out = seg_ret;
    return segno_ret;
}
void mst_$set_mstes(uid_t *uid, uint16_t obj_seg, uint32_t location,
                    uint16_t start, uint16_t end, uint16_t touch_count,
                    uint16_t asid, uint16_t prot, int8_t wired)
{
    n_set++; s_uid = uid; s_obj = obj_seg; s_loc = location; s_start = start;
    s_end = end; s_touch = touch_count; s_asid = asid; s_prot = prot; s_wired = wired;
}

#include "../map_canned_at.c"

static mste_t *entry(int page, int i)
{
    return (mste_t *)(void *)&pte_pages[(page - 1) * 0x400 + i * 0x10];
}

static uid_t u = { 9, 9 };
static status_$t st;

static void reset(void)
{
    memset(MST, 0, sizeof(MST));
    memset(pte_pages, 0, sizeof(pte_pages));
    ARCH_HOST_VA_BASE = (uintptr_t)pte_pages - (uintptr_t)MST_PAGE_TABLE_BASE;
    MST_ASID_BASE[0] = 0;
    MST[0] = 1; MST[1] = 2;
    segno_ret = 0; seg_ret = 0x3E; n_set = 0; st = 0x1234;
}

TEST(bad_segno)
{
    reset(); segno_ret = 0x3A;
    MST_$MAP_CANNED_AT(0, &u, 0, 0x8000, 0x00170001, true, false, 0, &st);
    ASSERT_EQ(status_$reference_to_illegal_address, st);
    ASSERT_EQ(0x3A, segno_asid);
    ASSERT_EQ(0, n_set);
}

TEST(maps_free_range)
{
    reset();
    /* offset 0x18000 (obj seg 3), size 0x10001: segs 3..5 = 3 segments */
    MST_$MAP_CANNED_AT(0x1F0000, &u, 0x18000, 0x10001, 0x00170002, true, false,
                       0xABCD, &st);
    ASSERT_EQ(status_$ok, st);
    ASSERT_EQ(1, n_set);
    ASSERT_EQ(3, s_obj);
    ASSERT_EQ(0x3E, s_start);
    ASSERT_EQ(0x40, s_end);
    ASSERT_EQ(2, s_touch);
    ASSERT_EQ(0, s_asid);
    ASSERT_EQ(0x17, s_prot);
    ASSERT_EQ(0xFF, (uint8_t)s_wired);
    ASSERT_EQ(0xABCD, s_loc);
    ASSERT_EQ((uintptr_t)&u, (uintptr_t)s_uid);
}

/* An occupied MSTE: an error unless `touch` and its first UID byte is 0. */
TEST(occupied)
{
    reset();
    entry(2, 0)->uid.high = 0x00001234u;      /* seg 0x40 */
    MST_$MAP_CANNED_AT(0, &u, 0, 0x18000, 0, false, false, 0, &st);
    ASSERT_EQ(status_$no_space_available, st);
    ASSERT_EQ(0, n_set);

    MST_$MAP_CANNED_AT(0, &u, 0, 0x18000, 0, false, true, 0, &st);
    ASSERT_EQ(status_$ok, st);

    entry(1, 0x3F)->uid.high = 0x01000000u;
    MST_$MAP_CANNED_AT(0, &u, 0, 0x18000, 0, false, true, 0, &st);
    ASSERT_EQ(status_$no_space_available, st);

    MST_$MAP_CANNED_AT(0, &u, 0, 0x8000, 0, false, false, 0, &st);  /* only 0x3E */
    ASSERT_EQ(status_$ok, st);
}

int main(void)
{
    printf("MST_$MAP_CANNED_AT tests\n");
    RUN_TEST(bad_segno);
    RUN_TEST(maps_free_range);
    RUN_TEST(occupied);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
