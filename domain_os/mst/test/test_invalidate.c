/*
 * mst/test/test_invalidate.c - unit tests for MST_$INVALIDATE (0x00E441CA)
 *
 * One host arena stands in for target memory from the AREA table
 * (0xD94C00) through the first MSTE pages (0xEF6400..), so the user range,
 * the area entries and the MST entries are all reached through
 * ARCH_VA_TO_PTR.  MST_$VA_TO_SEGNO is mocked: it returns mock_asid and
 * the slot (va >> 15) & 0x3f.
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
#include "math/math.h"
#include "anon/anon.h"

/* ---- data ------------------------------------------------------------ */

uint16_t MST[MST_TABLE_ENTRIES];
uint16_t MST_ASID_BASE[MST_MAX_ASIDS];
uint16_t MST_$SEG_PRIVATE_B;
uint16_t PROC1_$AS_ID;
uid_t UID_$NIL;
uid_t ANON_$UID;

#define ARENA_LO   0x00D94C00u
#define ARENA_SIZE (0x00EF7400u - ARENA_LO)
static uint8_t arena[ARENA_SIZE] __attribute__((aligned(0x400)));

#define USER_VA    0x00E00100u   /* slot 0, page 0, 0x100 into the page */

/* ---- mocks ----------------------------------------------------------- */

static int lock_calls, unlock_calls;
static uint16_t mock_asid, mock_asid_end;
static int segno_calls;
static int area_calls, ast_calls;
static int16_t area_gen;
static uint16_t area_id_arg, area_seg, area_page;
static uint32_t area_count, ast_start, ast_count;
static boolean area_zero, ast_zero;
static uid_t ast_uid;
static status_$t inval_status;

void ML_$LOCK(int16_t id) { (void)id; lock_calls++; }
void ML_$UNLOCK(int16_t id) { (void)id; unlock_calls++; }

uint16_t MST_$VA_TO_SEGNO(uint32_t va, uint16_t *segno_out, uint16_t dflt)
{
    (void)dflt;
    *segno_out = (uint16_t)((va >> 15) & 0x3F);
    segno_calls++;
    return (segno_calls == 2 && mock_asid_end != 0) ? mock_asid_end : mock_asid;
}

short M$OIS$WLW(long dividend, short divisor)
{
    return (short)(dividend % divisor);
}

void AREA_$INVALIDATE(int16_t gen, uint16_t area_id, uint16_t seg_idx,
                      uint16_t page_offset, uint32_t count,
                      boolean param_6, status_$t *status_ret)
{
    area_calls++;
    area_gen = gen;
    area_id_arg = area_id;
    area_seg = seg_idx;
    area_page = page_offset;
    area_count = count;
    area_zero = param_6;
    *status_ret = inval_status;
}

void AST_$INVALIDATE(uid_t *uid, uint32_t start_page, uint32_t count,
                     boolean flags, status_$t *status)
{
    ast_calls++;
    ast_uid = *uid;
    ast_start = start_page;
    ast_count = count;
    ast_zero = flags;
    *status = inval_status;
}

#include "../invalidate.c"

/* ---- helpers --------------------------------------------------------- */

static uint8_t *at(uint32_t va) { return arena + (va - ARENA_LO); }

static mst_entry_t *mste_slot(uint16_t slot)
{
    return (mst_entry_t *)(void *)at(MST_PAGE_TABLE_BASE + slot * 16);
}

static uid_t obj = { 0x01000000, 0x77 };

static void map_slots(int n, uint16_t first_seg, uint16_t prot)
{
    int i;
    for (i = 0; i < n; i++) {
        mste_slot((uint16_t)i)->uid = obj;
        mste_slot((uint16_t)i)->area_id = (uint16_t)(first_seg + i);
        mste_slot((uint16_t)i)->flags = (uint16_t)(prot << 9);
    }
}

static void reset_state(void)
{
    memset(MST, 0, sizeof(MST));
    memset(MST_ASID_BASE, 0, sizeof(MST_ASID_BASE));
    memset(arena, 0, sizeof(arena));
    MST_$SEG_PRIVATE_B = 0x1FF;
    PROC1_$AS_ID = 3;
    UID_$NIL.high = 0; UID_$NIL.low = 0;
    ANON_$UID.high = 0x00000011; ANON_$UID.low = 0;
    lock_calls = unlock_calls = 0;
    mock_asid = 3; mock_asid_end = 0;
    area_calls = ast_calls = 0;
    inval_status = status_$ok;
    MST_ASID_BASE[3] = 0x10;
    MST[0x10] = 1;
    ARCH_HOST_VA_BASE = (uintptr_t)arena - (uintptr_t)ARENA_LO;
}

static void run(uint32_t va, uint32_t len, uid_t *u, int8_t zero, status_$t *st)
{
    segno_calls = 0;
    MST_$INVALIDATE(&va, &len, u, &zero, st);
}

/* ---- tests ----------------------------------------------------------- */

static void test_zero_length_is_ok(void)
{
    status_$t st = 0x99;
    run(USER_VA, 0, &obj, 0, &st);
    ASSERT_EQ(status_$ok, st);
    ASSERT_EQ(0, lock_calls);
}

static void test_length_one_is_invalid(void)
{
    status_$t st = 0;
    run(USER_VA, 1, &obj, 0, &st);
    ASSERT_EQ(status_$mst_invalid_length, st);
}

static void test_range_into_private_b_is_illegal(void)
{
    status_$t st = 0;
    MST_$SEG_PRIVATE_B = 0x1C0;                 /* 0xE00000 */
    run(USER_VA, 0x10, &obj, 0, &st);
    ASSERT_EQ(status_$reference_to_illegal_address, st);
    ASSERT_EQ(0, lock_calls);
}

static void test_bad_asid_is_illegal(void)
{
    status_$t st = 0;
    mock_asid = 0x3A;
    run(USER_VA, 0x10, &obj, 0, &st);
    ASSERT_EQ(status_$reference_to_illegal_address, st);

    reset_state();
    mock_asid_end = 4;                          /* ends in another ASID */
    run(USER_VA, 0x10000, &obj, 0, &st);
    ASSERT_EQ(status_$reference_to_illegal_address, st);
    ASSERT_EQ(0, lock_calls);
}

static void test_unmapped_segment_is_illegal(void)
{
    status_$t st = 0;
    MST[0x10] = 0;
    run(USER_VA, 0x10, &obj, 0, &st);
    ASSERT_EQ(status_$reference_to_illegal_address, st);
    ASSERT_EQ(1, lock_calls);
    ASSERT_EQ(1, unlock_calls);
}

static void test_uid_mismatch_and_nil(void)
{
    status_$t st = 0;
    uid_t other = { 0x01000000, 0x78 };
    map_slots(1, 5, 7);
    run(USER_VA, 0x10, &other, 0, &st);
    ASSERT_EQ(status_$mst_uid_mismatch, st);

    run(USER_VA, 0x10, &UID_$NIL, 0, &st);     /* NIL matches anything */
    ASSERT_EQ(status_$ok, st);
}

static void test_insufficient_rights(void)
{
    status_$t st = 0;
    map_slots(1, 5, 5);
    run(USER_VA, 0x10, &obj, 0, &st);
    ASSERT_EQ(status_$mst_insufficient_rights, st);
    ASSERT_EQ(1, unlock_calls);
}

static void test_nonconsecutive_segments(void)
{
    status_$t st = 0;
    map_slots(2, 5, 6);
    mste_slot(1)->area_id = 9;
    run(USER_VA, 0x8000, &obj, 0, &st);         /* spans slots 0 and 1 */
    ASSERT_EQ(status_$mst_uid_mismatch, st);
}

static void test_ast_path_zero_fill(void)
{
    status_$t st = 0x99;
    uint32_t i;

    map_slots(1, 5, 7);
    memset(at(USER_VA - 0x100), 0xAA, 0x1000);
    run(USER_VA, 0x900, &obj, (int8_t)0xFF, &st);
    ASSERT_EQ(status_$ok, st);
    ASSERT_EQ(1, ast_calls);
    ASSERT_EQ(5 * 32 + 1, ast_start);          /* page after the partial one */
    ASSERT_EQ(1, ast_count);
    ASSERT_EQ((uint8_t)0xFF, (uint8_t)ast_zero);
    ASSERT_EQ(0x77, ast_uid.low);
    for (i = 0; i < 0x300; i++) {
        ASSERT_EQ(0, at(USER_VA)[i]);           /* head of first page */
    }
    ASSERT_EQ(0xAA, at(USER_VA)[0x300]);       /* the whole page is left */
    for (i = 0x700; i < 0x900; i++) {
        ASSERT_EQ(0, at(USER_VA)[i]);           /* tail partial page */
    }
    ASSERT_EQ(0xAA, at(USER_VA)[0x900]);
    ASSERT_EQ(0xAA, at(USER_VA)[-1]);
}

static void test_ast_path_no_zero(void)
{
    status_$t st = 0x99;
    map_slots(1, 5, 6);
    memset(at(USER_VA), 0xAA, 0x900);
    run(USER_VA, 0x900, &obj, 0, &st);
    ASSERT_EQ(status_$ok, st);
    ASSERT_EQ(0xAA, at(USER_VA)[0]);
    ASSERT_EQ(0xAA, at(USER_VA)[0x8FF]);
    ASSERT_EQ(1, ast_calls);
}

static void test_short_range_within_one_page(void)
{
    status_$t st = 0;
    map_slots(1, 5, 7);
    memset(at(USER_VA), 0xAA, 0x20);
    run(USER_VA, 0x13, &obj, (int8_t)0xFF, &st);
    ASSERT_EQ(status_$ok, st);
    ASSERT_EQ(0, ast_calls);                   /* no whole page */
    ASSERT_EQ(0, at(USER_VA)[0x12]);
    ASSERT_EQ(0xAA, at(USER_VA)[0x13]);
}

static void test_area_path_reversed(void)
{
    status_$t st = 0;
    area_$entry_t *area;

    map_slots(1, 2, 7);
    mste_slot(0)->uid = ANON_$UID;
    mste_slot(0)->uid.low = 0x00040003;         /* gen 4, area 3 */
    area = (area_$entry_t *)(void *)at(AREA_TABLE_BASE + 2 * AREA_ENTRY_SIZE);
    area->flags = MST_INVALIDATE_AREA_REVERSED;
    run(USER_VA - 0x100 + 0x800, 0x800, &UID_$NIL, 0, &st);   /* pages 2, 3 */
    ASSERT_EQ(status_$ok, st);
    ASSERT_EQ(0, ast_calls);
    ASSERT_EQ(1, area_calls);
    ASSERT_EQ(4, area_gen);
    ASSERT_EQ(3, area_id_arg);
    ASSERT_EQ(2, area_seg);
    ASSERT_EQ(((0xFFFF - 2) * 32 + 2) & 0x1F, area_page);
    ASSERT_EQ(2, area_count);
}

static void test_area_page_carry_bumps_segment(void)
{
    status_$t st = 0;

    map_slots(2, 2, 7);
    mste_slot(0)->uid = ANON_$UID;
    mste_slot(0)->uid.low = 0x00040003;
    mste_slot(1)->uid = ANON_$UID;
    /* start 0x10 before the end of page 31: the partial page rolls over */
    run(USER_VA - 0x100 + 0x400 * 0x1F + 0x3F0, 0x410, &UID_$NIL, 0, &st);
    ASSERT_EQ(status_$ok, st);
    ASSERT_EQ(1, area_calls);
    ASSERT_EQ(3, area_seg);                    /* D4w incremented */
    ASSERT_EQ(0, area_page);
    ASSERT_EQ(1, area_count);
}

static void test_invalidate_failure_returned(void)
{
    status_$t st = 0;
    map_slots(1, 5, 7);
    inval_status = 0x00030006;
    run(USER_VA - 0x100, 0x800, &obj, (int8_t)0xFF, &st);
    ASSERT_EQ(0x00030006, st);
}

int main(void)
{
    printf("MST_$INVALIDATE tests:\n");
    RUN_TEST(zero_length_is_ok);
    RUN_TEST(length_one_is_invalid);
    RUN_TEST(range_into_private_b_is_illegal);
    RUN_TEST(bad_asid_is_illegal);
    RUN_TEST(unmapped_segment_is_illegal);
    RUN_TEST(uid_mismatch_and_nil);
    RUN_TEST(insufficient_rights);
    RUN_TEST(nonconsecutive_segments);
    RUN_TEST(ast_path_zero_fill);
    RUN_TEST(ast_path_no_zero);
    RUN_TEST(short_range_within_one_page);
    RUN_TEST(area_path_reversed);
    RUN_TEST(area_page_carry_bumps_segment);
    RUN_TEST(invalidate_failure_returned);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
