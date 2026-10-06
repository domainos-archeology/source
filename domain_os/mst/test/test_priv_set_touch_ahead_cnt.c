/*
 * mst/test/test_priv_set_touch_ahead_cnt.c - MST_$PRIV_SET_TOUCH_AHEAD_CNT (0x00E44514)
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
uint16_t MST_$TOUCH_COUNT = 5;
uint16_t MST_$PRIVATE_A_SIZE = 0x100;
uint16_t MST_$GLOBAL_A_SIZE = 0x80;
uint16_t MST_$SEG_TN = 0x200;
uint16_t PROC1_$AS_ID = 3;
static uint8_t pte_pages[4 * 0x400] __attribute__((aligned(0x400)));
static int nlock, nunlock;

static uint16_t segno_asid, segno_seg;
uint16_t MST_$VA_TO_SEGNO(uint32_t va, uint16_t *seg, uint16_t dflt)
{
    (void)va;
    if (dflt != 3) { current_failed = 1; }
    *seg = segno_seg;
    return segno_asid;
}
void ML_$LOCK(int16_t id) { if (id != 0xC) current_failed = 1; nlock++; }
void ML_$UNLOCK(int16_t id) { if (id != 0xC) current_failed = 1; nunlock++; }

#include "../priv_set_touch_ahead_cnt.c"

static mste_t *entry(int page, int i)
{
    return (mste_t *)(void *)&pte_pages[(page - 1) * 0x400 + i * 0x10];
}

static uint16_t flags0 = 0, flags_priv = 0x8000;
static uint32_t va, len;
static int16_t cnt;
static uint16_t result;
static status_$t st;

static void reset(uint16_t asid, uint16_t seg)
{
    memset(MST, 0, sizeof(MST));
    memset(pte_pages, 0, sizeof(pte_pages));
    ARCH_HOST_VA_BASE = (uintptr_t)pte_pages - (uintptr_t)MST_PAGE_TABLE_BASE;
    MST_ASID_BASE[3] = 0x20;
    MST[0x20] = 1;
    MST[0x21] = 2;
    segno_asid = asid;
    segno_seg = seg;
    nlock = nunlock = 0;
    result = 0xAAAA;
    st = 0x1234;
}

TEST(sets_count_across_a_page_boundary)
{
    reset(3, 0x3F);
    entry(1, 0x3F)->location = 0x83000000u | (4u << 2 << 24);    /* old 5 */
    va = 0x7F00; len = 0x200;   /* 0x7F00 + 0x200 - 1 -> one extra segment */
    cnt = 9;
    MST_$PRIV_SET_TOUCH_AHEAD_CNT(&flags0, &va, &len, &cnt, &result, &st);
    ASSERT_EQ(0, st);
    ASSERT_EQ(1, nlock); ASSERT_EQ(1, nunlock);
    ASSERT_EQ(0x83000000u | (8u << 26), entry(1, 0x3F)->location);
    ASSERT_EQ(8u << 26, entry(2, 0)->location);
    ASSERT_EQ(1, result);           /* the last segment's old count (0 + 1) */
}

TEST(count_specials_and_clamp)
{
    reset(3, 1);
    va = 0; len = 1;
    cnt = -2;                       /* MST_$TOUCH_COUNT */
    MST_$PRIV_SET_TOUCH_AHEAD_CNT(&flags0, &va, &len, &cnt, &result, &st);
    ASSERT_EQ(4u << 26, entry(1, 1)->location);
    cnt = -3;
    MST_$PRIV_SET_TOUCH_AHEAD_CNT(&flags0, &va, &len, &cnt, &result, &st);
    ASSERT_EQ(0x1Fu << 26, entry(1, 1)->location);
    ASSERT_EQ(5, result);
    cnt = 100;                      /* clamped to 0x20 */
    MST_$PRIV_SET_TOUCH_AHEAD_CNT(&flags0, &va, &len, &cnt, &result, &st);
    ASSERT_EQ(0x1Fu << 26, entry(1, 1)->location);
    cnt = -7;                       /* clamped to 1 */
    MST_$PRIV_SET_TOUCH_AHEAD_CNT(&flags0, &va, &len, &cnt, &result, &st);
    ASSERT_EQ(0, entry(1, 1)->location);
    ASSERT_EQ(0x20, result);
    cnt = -1;                       /* read only */
    MST_$PRIV_SET_TOUCH_AHEAD_CNT(&flags0, &va, &len, &cnt, &result, &st);
    ASSERT_EQ(0, entry(1, 1)->location);
    ASSERT_EQ(1, result);
    ASSERT_EQ(0, st);
}

TEST(limits_and_privilege)
{
    reset(3, 0x100);                /* past the private A size */
    va = 0; len = 1; cnt = 2;
    MST_$PRIV_SET_TOUCH_AHEAD_CNT(&flags0, &va, &len, &cnt, &result, &st);
    ASSERT_EQ(status_$reference_to_illegal_address, st);
    ASSERT_EQ(0xAAAA, result);
    ASSERT_EQ(0, nlock);
    reset(0x3A, 1);                 /* asid > 0x39 */
    MST_$PRIV_SET_TOUCH_AHEAD_CNT(&flags_priv, &va, &len, &cnt, &result, &st);
    ASSERT_EQ(status_$reference_to_illegal_address, st);
    reset(3, 0xFF);                 /* last private segment, then one past */
    len = 0x8001;
    MST_$PRIV_SET_TOUCH_AHEAD_CNT(&flags0, &va, &len, &cnt, &result, &st);
    ASSERT_EQ(status_$reference_to_illegal_address, st);
}

TEST(missing_page_table_page)
{
    reset(3, 0x40);                 /* group 1 -> MST[0x21] */
    MST[0x21] = 0;
    va = 0; len = 1; cnt = 2;
    MST_$PRIV_SET_TOUCH_AHEAD_CNT(&flags0, &va, &len, &cnt, &result, &st);
    ASSERT_EQ(status_$reference_to_illegal_address, st);
    ASSERT_EQ(1, nunlock);
    ASSERT_EQ(0x21, result);        /* D2 still the group's MST index */
}

int main(void)
{
    printf("MST_$PRIV_SET_TOUCH_AHEAD_CNT\n");
    RUN_TEST(sets_count_across_a_page_boundary);
    RUN_TEST(count_specials_and_clamp);
    RUN_TEST(limits_and_privilege);
    RUN_TEST(missing_page_table_page);
    printf("%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
