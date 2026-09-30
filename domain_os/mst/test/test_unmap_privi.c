/*
 * mst/test/test_unmap_privi.c - unit tests for MST_$UNMAP_PRIVI (0x00E448B0)
 *
 * The MSTE pages (VA 0xEF6000..) live in a local arena through
 * ARCH_HOST_VA_BASE; MST page 1 is the arena page at VA 0xEF6400.
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

static int tests_passed = 0;
static int tests_failed = 0;

static void reset_state(void);

#define RUN_TEST(name) do { \
    printf("  Running %s... ", #name); \
    reset_state(); \
    test_##name(); \
    printf("PASSED\n"); \
    tests_passed++; \
} while (0)

#define ASSERT_EQ(expected, actual) do { \
    if ((unsigned long)(expected) != (unsigned long)(actual)) { \
        printf("FAILED\n    Expected: 0x%lx, Got: 0x%lx at line %d\n", \
               (unsigned long)(expected), (unsigned long)(actual), __LINE__); \
        tests_failed++; \
        return; \
    } \
} while (0)

#include "mst/mst_internal.h"
#include "anon/anon.h"

/* ---- data ------------------------------------------------------------ */

uint16_t MST[MST_TABLE_ENTRIES];
uint16_t MST_ASID_BASE[MST_MAX_ASIDS];
uint16_t MST_$SEG_PRIVATE_B;
uid_t ANON_$UID = { 0xA0A00000u, 0 };
static uint8_t arena[0x1000] __attribute__((aligned(0x400)));

/* ---- mocks ----------------------------------------------------------- */

static int n_lock, n_unlock, n_area, n_seg;
static uint16_t first_seg, last_seg, segno1, segno2;
static int segno_calls;
static uint32_t seg_va[4];
static int8_t area_flag;
static uint32_t area_handle;
static int8_t seg_flag;

uint16_t MST_$VA_TO_SEGNO(uint32_t va, uint16_t *seg_out, uint16_t asid)
{
    (void)va; (void)asid;
    *seg_out = (segno_calls == 0) ? first_seg : last_seg;
    return (segno_calls++ == 0) ? segno1 : segno2;
}
void ML_$LOCK(int16_t id) { (void)id; n_lock++; }
void ML_$UNLOCK(int16_t id) { (void)id; n_unlock++; }
void AREA_$REMOVE_SEG(void *rec, uint16_t a, uint16_t b, int8_t c, uint16_t d,
                      status_$t *st)
{
    (void)a; (void)b; (void)d;
    n_area++; area_flag = c; area_handle = *(uint32_t *)rec; *st = 0;
}
void MST_$REMOVE_SEG(locate_request_t *r, uint32_t va, uint16_t a, uint16_t b,
                     boolean f)
{
    (void)r; (void)a; (void)b;
    seg_va[n_seg++ & 3] = va; seg_flag = (int8_t)f;
}

#include "../unmap_privi.c"

/* ---- helpers --------------------------------------------------------- */

static mste_t *entry(int page, int seg)
{
    return (mste_t *)(void *)&arena[0x400 * page + 0x10 * (seg & 0x3F)];
}

static void reset_state(void)
{
    memset(arena, 0, sizeof(arena));
    memset(MST, 0, sizeof(MST));
    memset(MST_ASID_BASE, 0, sizeof(MST_ASID_BASE));
    ARCH_HOST_VA_BASE = (uintptr_t)arena - 0xEF6000u;
    MST_$SEG_PRIVATE_B = 0x100;
    n_lock = n_unlock = n_area = n_seg = 0;
    segno_calls = 0;
    segno1 = segno2 = 3;
    first_seg = 2; last_seg = 3;
    MST_ASID_BASE[3] = 0x10;
    MST[0x10] = 1;                              /* segments 0..63 -> page 1 */
}

/* ---- tests ----------------------------------------------------------- */

static void test_private_b_check(void)
{
    uid_t u = { 1, 2 };
    status_$t st;
    MST_$UNMAP_PRIVI(0, &u, 0x100u << 15, 0x8000, 1, &st);
    ASSERT_EQ(0x00040004, st);
    ASSERT_EQ(0, n_lock);
    /* bit 0 lifts it */
    segno1 = 3; segno2 = 4;
    MST_$UNMAP_PRIVI(1, &u, 0x100u << 15, 0x8000, 1, &st);
    ASSERT_EQ(0x00040004, st);                  /* segno mismatch */
}

/* 0x00E44924-0x00E4494A: the end VA is translated before the 0x39 test */
static void test_both_translations_run(void)
{
    uid_t u = { 1, 2 };
    status_$t st;
    segno1 = 0x40; segno2 = 0x40;
    MST_$UNMAP_PRIVI(1, &u, 0x100u << 15, 0x8000, 1, &st);
    ASSERT_EQ(0x00040004, st);
    ASSERT_EQ(2, segno_calls);
    ASSERT_EQ(0, n_lock);
}

static void test_zero_length(void)
{
    uid_t u = { 1, 2 };
    status_$t st;
    MST_$UNMAP_PRIVI(0, &u, 0x10000, 0, 1, &st);
    ASSERT_EQ(0x00040002, st);
}

static void test_object_segments_cleared(void)
{
    uid_t u = { 0x1234, 5 };
    status_$t st;
    entry(1, 2)->uid = u;
    entry(1, 2)->unknown_0a = 0xFFFF;
    entry(1, 2)->location = 0x7C000000u;       /* field 0x1F */
    entry(1, 3)->uid = u;
    MST_$UNMAP_PRIVI(0, &u, 0x10000, 0x10000, 1, &st);
    ASSERT_EQ(0, st);
    ASSERT_EQ(2, n_seg);
    ASSERT_EQ(0x10000, seg_va[0]);
    ASSERT_EQ(0x18000, seg_va[1]);
    ASSERT_EQ(0, entry(1, 2)->uid.high);
    ASSERT_EQ(0xFE00, entry(1, 2)->unknown_0a);
    ASSERT_EQ(1, n_lock);
    ASSERT_EQ(1, n_unlock);
}

static void test_keep_entry_marks(void)
{
    uid_t u = { 0x1234, 5 };
    status_$t st;
    last_seg = 2;
    entry(1, 2)->uid = u;
    MST_$UNMAP_PRIVI(4, &u, 0x10000, 0x8000, 1, &st);
    ASSERT_EQ(0, st);
    ASSERT_EQ(0x1234, entry(1, 2)->uid.high);
    ASSERT_EQ(0x4000, entry(1, 2)->unknown_0a);
    ASSERT_EQ(0, (uint8_t)seg_flag);
}

static void test_area_segment(void)
{
    uid_t u = { 0xA0A00000u, 0x00030007 };
    status_$t st;
    last_seg = 2;
    entry(1, 2)->uid = u;
    MST_$UNMAP_PRIVI(0x10, &u, 0x10000, 0x8000, 1, &st);
    ASSERT_EQ(0, st);
    ASSERT_EQ(1, n_area);
    ASSERT_EQ(0x00030007, area_handle);
    ASSERT_EQ(0xFF, (uint8_t)area_flag);
}

static void test_areas_only_rejects_object(void)
{
    uid_t u = { 0x1234, 5 };
    status_$t st;
    entry(1, 2)->uid = u;
    MST_$UNMAP_PRIVI(0x10, &u, 0x10000, 0x10000, 1, &st);
    ASSERT_EQ(0x00040012, st);
    ASSERT_EQ(0, n_seg);
    ASSERT_EQ(1, n_unlock);
}

static void test_match_uid(void)
{
    uid_t u = { 0x1234, 5 };
    uid_t other = { 0x9999, 5 };
    status_$t st;
    entry(1, 2)->uid = other;
    MST_$UNMAP_PRIVI(2, &u, 0x10000, 0x10000, 1, &st);
    ASSERT_EQ(0x00040007, st);
    ASSERT_EQ(0, n_seg);
}

static void test_missing_page_skips_block(void)
{
    uid_t u = { 0x1234, 5 };
    status_$t st;
    MST[0x10] = 0;                              /* no page for segs 0..63 */
    MST[0x11] = 1;                              /* segs 64..127 -> page 1 */
    first_seg = 2; last_seg = 0x41;
    entry(1, 0x40)->uid = u;
    MST_$UNMAP_PRIVI(0, &u, 0x10000, 0x200000, 1, &st);
    ASSERT_EQ(0, st);
    ASSERT_EQ(1, n_seg);
    ASSERT_EQ(0x10000u + 0x200000u, seg_va[0]); /* quirk: a full block */
}

int main(void)
{
    printf("MST_$UNMAP_PRIVI tests:\n");
    RUN_TEST(private_b_check);
    RUN_TEST(both_translations_run);
    RUN_TEST(zero_length);
    RUN_TEST(object_segments_cleared);
    RUN_TEST(keep_entry_marks);
    RUN_TEST(area_segment);
    RUN_TEST(areas_only_rejects_object);
    RUN_TEST(match_uid);
    RUN_TEST(missing_page_skips_block);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
