/*
 * area/test/test_find_entry_by_uid.c - unit tests for
 * area_$find_entry_by_uid (0x00E0939C)
 *
 * The area table is a host array; area_$get_aste, the ML lock, CRASH_SYSTEM
 * and M$OIS$WLW are mocked.
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <setjmp.h>

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

#include "area/area_internal.h"
#include "misc/crash_system.h"

#define TABLE_N 8
static area_$entry_t mock_area_table[TABLE_N];
#undef AREA_TABLE_BASE
#define AREA_TABLE_BASE ((uintptr_t)mock_area_table)
#undef AREA_ENTRY_SIZE
#define AREA_ENTRY_SIZE ((int)sizeof(area_$entry_t))

area_$globals_t AREA_$GLOBALS;
status_$t Area_Internal_Error = 0x0032000A;

static int locks, unlocks, get_calls, crashes;
static int16_t get_area, get_seg;
static int8_t get_held, get_create;
static area_$seg_slot_t *get_slot;
static aste_t the_aste;
static jmp_buf crash_jmp;

void ML_$LOCK(int16_t id) { (void)id; locks++; }
void ML_$UNLOCK(int16_t id) { (void)id; unlocks++; }
void CRASH_SYSTEM(const status_$t *s) { (void)s; crashes++; longjmp(crash_jmp, 1); }
short M$OIS$WLW(long dividend, short divisor) { return (short)(dividend % divisor); }

struct aste_t *area_$get_aste(int16_t area_id, area_$seg_slot_t *slot,
                              int16_t seg_idx, int8_t in_trans_held,
                              int8_t create, status_$t *status_p)
{
    get_calls++;
    get_area = area_id; get_slot = slot; get_seg = seg_idx;
    get_held = in_trans_held; get_create = create;
    *status_p = status_$ok;
    return &the_aste;
}

#include "../find_entry_by_uid.c"

static area_$entry_t *e;
/* the VA-linked records live in one arena that ARCH_HOST_VA_BASE covers */
static struct {
    area_$seg_table_t a, b;
    uint32_t overflow[0x100];
} arena;
#define tbl_a    arena.a
#define tbl_b    arena.b
#define overflow arena.overflow

static void reset_state(void)
{
    memset(mock_area_table, 0, sizeof(mock_area_table));
    memset(&AREA_$GLOBALS, 0, sizeof(AREA_$GLOBALS));
    memset(&tbl_a, 0, sizeof(tbl_a));
    memset(&tbl_b, 0, sizeof(tbl_b));
    locks = unlocks = get_calls = crashes = 0;
    e = &mock_area_table[2];            /* area id 3 */
    e->flags = AREA_FLAG_ACTIVE;
    e->virt_size = 0x400000;            /* 0x1000 pages */
    e->owner_asid = 5;
}

static void test_inline_slot(void)
{
    status_$t st = 0x77;
    uint16_t bste = 9;
    aste_t *r = area_$find_entry_by_uid(3, &bste, 4, &st);
    /* page 9*32 + 4 = 0x124 -> slot 1 */
    ASSERT_EQ((unsigned long)&the_aste, (unsigned long)r);
    ASSERT_EQ(0, st);
    ASSERT_EQ((unsigned long)&e->seg_bitmap[1], (unsigned long)get_slot);
    ASSERT_EQ(3, get_area);
    ASSERT_EQ(9, get_seg);
    ASSERT_EQ(0, get_held);
    ASSERT_EQ(-1, get_create);
    ASSERT_EQ(1, locks);
    ASSERT_EQ(1, unlocks);
}

static void test_not_active(void)
{
    status_$t st;
    uint16_t bste = 0;
    aste_t *r;
    e->flags = 0;
    r = area_$find_entry_by_uid(3, &bste, 0, &st);
    ASSERT_EQ(0x00320006, st);
    ASSERT_EQ((unsigned long)&bste, (unsigned long)r);
    ASSERT_EQ(0, get_calls);
}

static void test_beyond_size(void)
{
    status_$t st;
    uint16_t bste = 2;                  /* page 0x40: one past 0x40 pages */
    e->virt_size = 0x10000;
    area_$find_entry_by_uid(3, &bste, 0, &st);
    ASSERT_EQ(0x00030001, st);
    ASSERT_EQ(0, get_calls);
    bste = 1;
    area_$find_entry_by_uid(3, &bste, 0x1F, &st);
    ASSERT_EQ(0, st);
    ASSERT_EQ(1, get_calls);
}

static void test_reversed_rewrites_bste(void)
{
    status_$t st;
    uint16_t bste = 0xFFFE;             /* -2 -> 1 */
    e->flags = AREA_FLAG_ACTIVE | AREA_FLAG_REVERSED;
    area_$find_entry_by_uid(3, &bste, 0x1F, &st);
    ASSERT_EQ(1, bste);
    ASSERT_EQ(0, st);
    /* page 1*32 + 0x1F = 0x3F -> slot 0 */
    ASSERT_EQ((unsigned long)&e->seg_bitmap[0], (unsigned long)get_slot);

    reset_state();
    e->flags = AREA_FLAG_ACTIVE | AREA_FLAG_REVERSED;
    bste = 0xFF7F;                      /* -> 0x80: page 0x101F - 0 > 0xFFF */
    area_$find_entry_by_uid(3, &bste, 0, &st);
    ASSERT_EQ(0x00030001, st);
}

static void test_overflow_table(void)
{
    status_$t st;
    uint16_t bste = 0x30;               /* page 0x600 -> n 6 -> table 0, cell 4 */
    ARCH_HOST_VA_BASE = (uintptr_t)&arena - 0x1000u;
    tbl_b.area_id = 3; tbl_b.table_index = 0;
    tbl_b.bitmap_ptr = ARCH_PTR_TO_VA(overflow);
    tbl_a.area_id = 4; tbl_a.table_index = 0;
    tbl_a.next = ARCH_PTR_TO_VA(&tbl_b);
    AREA_$GLOBALS.seg_table_list[5] = &tbl_a;

    area_$find_entry_by_uid(3, &bste, 0, &st);
    ASSERT_EQ((unsigned long)&overflow[4], (unsigned long)get_slot);
}

static void test_missing_overflow_table_crashes(void)
{
    status_$t st;
    uint16_t bste = 0x30;
    if (setjmp(crash_jmp) == 0) {
        area_$find_entry_by_uid(3, &bste, 0, &st);
    }
    ASSERT_EQ(1, crashes);
    ASSERT_EQ(0, get_calls);
}

int main(void)
{
    printf("area_$find_entry_by_uid tests:\n");
    RUN_TEST(inline_slot);
    RUN_TEST(not_active);
    RUN_TEST(beyond_size);
    RUN_TEST(reversed_rewrites_bste);
    RUN_TEST(overflow_table);
    RUN_TEST(missing_overflow_table_crashes);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
