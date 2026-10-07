/*
 * mst/test/test_alloc_segs.c - unit tests for mst_$alloc_segs (0x00E43182)
 * and its nested procedure mst_$audit_map (0x00E430D0)
 *
 * MST, MST_ASID_BASE and four page-table pages are host objects (the pages
 * through ARCH_HOST_VA_BASE); the area table is a host array; every callee
 * is mocked.
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

/* MST_PAGE_TABLE_BASE is ARCH_PTR_TO_VA(MSTE_PAGES), a sau2.ld symbol past
 * OS_PAGE_END since source-o7s2 (docs/rfc-cold-start.md section 8c); this
 * test stands in for the link with the map's value, `EF6400 MSTE_PAGES'
 * (os/test/test_vm_tables.c checks the macro itself). */
#undef MST_PAGE_TABLE_BASE
#define MST_PAGE_TABLE_BASE 0x00EF6400u
#include "anon/anon.h"
#include "audit/audit.h"

#define TABLE_N 4
static area_$entry_t mock_area_table[TABLE_N];
#undef AREA_TABLE_BASE
#define AREA_TABLE_BASE ((uintptr_t)mock_area_table)
#undef AREA_ENTRY_SIZE
#define AREA_ENTRY_SIZE ((int)sizeof(area_$entry_t))

uint16_t MST[MST_TABLE_ENTRIES];
uint16_t MST_ASID_BASE[MST_MAX_ASIDS];
uint16_t MST_$SEG_TN, MST_$GLOBAL_A_SIZE, MST_$SEG_GLOBAL_A, MST_$SEG_GLOBAL_A_END;
uint16_t MST_$PRIVATE_A_SIZE, MST_$SEG_PRIVATE_A_END, MST_$SEG_PRIVATE_B_OFFSET;
uint16_t MST_$SEG_GLOBAL_B_OFFSET, MST_$SEG_HIGH;
uint16_t PROC1_$AS_ID;
uid_t ANON_$UID;
int8_t AUDIT_$ENABLED;

static uint8_t pte_pages[4 * 0x400] __attribute__((aligned(0x400)));

/* ---- mocks ----------------------------------------------------------- */

static int locks, unlocks, lookups, allocs, threads, sets, audits;
static uint32_t lk_location, lk_len; static status_$t lk_status;
static uint16_t lk_prot; static uint32_t lk_size;
static uint16_t next_page;
static status_$t alloc_status;
static uint16_t al_seg[8];
static uint32_t th_handle, th_words; static int16_t th_asid, th_start;
static status_$t th_status;
static uid_t *s_uid; static uint16_t s_obj, s_start, s_end, s_touch, s_asid, s_prot;
static uint32_t s_loc; static int8_t s_wired;
static uid_t au_event; static uint16_t au_flag, au_data[4];

void ML_$LOCK(int16_t id) { (void)id; locks++; }
void ML_$UNLOCK(int16_t id) { (void)id; unlocks++; }

void mst_$lookup_object(uid_t *uid, uint16_t prot, uint32_t area_size,
                        uint32_t *location, uint32_t *obj_length,
                        status_$t *status)
{
    (void)uid;
    lookups++; lk_prot = prot; lk_size = area_size;
    *location = lk_location; *obj_length = lk_len; *status = lk_status;
}

status_$t MST_$ALLOC_TABLE_PAGE(uint16_t asid, uint16_t flags, uint16_t *table_ptr)
{
    (void)asid;
    al_seg[allocs & 7] = flags;
    allocs++;
    if (alloc_status == status_$ok) {
        *table_ptr = next_page++;
    }
    return alloc_status;
}

void AREA_$THREAD_BSTES(area_$handle_t *handle_ptr, int16_t bste_idx,
                        int16_t seg_idx, uint32_t param_4,
                        status_$t *status_ret)
{
    threads++; th_handle = *handle_ptr; th_asid = bste_idx; th_start = seg_idx;
    th_words = param_4; *status_ret = th_status;
}

void mst_$set_mstes(uid_t *uid, uint16_t obj_seg, uint32_t location,
                    uint16_t start, uint16_t end, uint16_t touch_count,
                    uint16_t asid, uint16_t prot, int8_t wired)
{
    sets++; s_uid = uid; s_obj = obj_seg; s_loc = location; s_start = start;
    s_end = end; s_touch = touch_count; s_asid = asid; s_prot = prot; s_wired = wired;
}

void AUDIT_$LOG_EVENT(uid_t *event_uid, uint16_t *event_flags,
                      status_$t *status, char *data, const uint16_t *data_len)
{
    (void)status; (void)data_len;
    audits++; au_event = *event_uid; au_flag = *event_flags;
    memcpy(au_data, data, 8);
}

#include "../alloc_segs.c"

static mste_t *mste(int page, int i)
{
    return (mste_t *)(void *)&pte_pages[(page - 1) * 0x400 + i * 0x10];
}

static uid_t obj = { 0x11112222, 0x33334444 };
static uint32_t info;

static void reset_state(void)
{
    memset(MST, 0, sizeof(MST));
    memset(MST_ASID_BASE, 0, sizeof(MST_ASID_BASE));
    memset(pte_pages, 0, sizeof(pte_pages));
    memset(mock_area_table, 0, sizeof(mock_area_table));
    ARCH_HOST_VA_BASE = (uintptr_t)pte_pages - (uintptr_t)MST_PAGE_TABLE_BASE;
    MST_$PRIVATE_A_SIZE = 0x80;   MST_$SEG_TN = 0x88;
    MST_$SEG_PRIVATE_A_END = 0x7F; MST_$SEG_PRIVATE_B_OFFSET = 0x100;
    MST_$GLOBAL_A_SIZE = 0x40;    MST_$SEG_GLOBAL_A = 0x200;
    MST_$SEG_GLOBAL_A_END = 0x23F; MST_$SEG_HIGH = 0x300;
    MST_$SEG_GLOBAL_B_OFFSET = 0x2C0;
    MST_ASID_BASE[3] = 4;
    PROC1_$AS_ID = 3;
    ANON_$UID.high = 0xA0A0A0A0;
    AUDIT_$ENABLED = 0;
    locks = unlocks = lookups = allocs = threads = sets = audits = 0;
    lk_location = 0x1234; lk_len = 0x100000; lk_status = status_$ok;
    next_page = 1;
    alloc_status = status_$ok;
    th_status = status_$ok;
    info = 0;
}

/* ---- tests ----------------------------------------------------------- */

static void test_zero_length(void)
{
    status_$t st;
    void *r = mst_$alloc_segs(0x7FFFFFFF, &obj, 0, 0, 0, 3, 5, 1, 0, 0, &info, &st);
    ASSERT_EQ(0x00040002, st);
    /* the return is the never-written (-0x20,A6) cell on this path */
    ASSERT_EQ(0, locks);
}

static void test_top_down_private(void)
{
    status_$t st;
    void *r;

    /* 0x17000 bytes from offset 0x9000: object segments 1..3 */
    r = mst_$alloc_segs(0x7FFFFFFF, &obj, 0x9000, 0x17000, 0x77, 3, 5, 2,
                        0, 0, &info, &st);
    ASSERT_EQ(0, st);
    ASSERT_EQ(1, lookups);
    ASSERT_EQ(5, lk_prot);
    ASSERT_EQ(0x77, lk_size);
    /* the window is [0, 0x80): candidate 0x7D..0x7F; MST[4+1] is empty so
     * one page is allocated for segment 0x7D */
    ASSERT_EQ(1, allocs);
    ASSERT_EQ(0x7D, al_seg[0]);
    ASSERT_EQ(1, MST[5]);
    ASSERT_EQ(1, sets);
    ASSERT_EQ(1, s_obj);
    ASSERT_EQ(0x7D, s_start);
    ASSERT_EQ(0x7F, s_end);
    ASSERT_EQ(2, s_touch);
    ASSERT_EQ(3, s_asid);
    ASSERT_EQ(5, s_prot);
    ASSERT_EQ(0x1234, s_loc);
    ASSERT_EQ(0x17000, info);
    ASSERT_EQ((0x7Du << 15) + 0x1000, (uint32_t)((uintptr_t)r - ARCH_HOST_VA_BASE));
    ASSERT_EQ(locks, unlocks);
}

static void test_top_down_skips_used(void)
{
    status_$t st;
    void *r;

    MST[5] = 1;                                   /* segs 0x40..0x7F */
    mste(1, 0x3F)->uid.high = 1;                  /* 0x7F in use */
    r = mst_$alloc_segs(0x7FFFFFFF, &obj, 0, 0x10000, 0, 3, 5, 1, 0, 0, &info, &st);
    ASSERT_EQ(0, st);
    ASSERT_EQ(0x7D, s_start);
    ASSERT_EQ(0x7E, s_end);
    ASSERT_EQ(0, allocs);
    (void)r;
}

static void test_bottom_up_global(void)
{
    status_$t st;
    void *r;

    MST[0] = 2;                                   /* asid 0 base 0 */
    mste(2, 0)->uid.high = 1;                     /* seg 0 in use */
    r = mst_$alloc_segs(0, &obj, 0, 0x8000, 0, 0, 5, 1, 0, 0, &info, &st);
    ASSERT_EQ(0, st);
    ASSERT_EQ(1, s_start);
    ASSERT_EQ(1, s_end);
    ASSERT_EQ(0, s_asid);
    /* global A: + MST_$SEG_GLOBAL_A */
    ASSERT_EQ(0x201u << 15, (uint32_t)((uintptr_t)r - ARCH_HOST_VA_BASE));
}

static void test_fixed_global_address_forces_asid_0(void)
{
    status_$t st;
    void *r;

    MST[0] = 2;
    r = mst_$alloc_segs(0x205u << 15, &obj, 0, 0x8000, 0, 3, 5, 1, 0, 0, &info, &st);
    ASSERT_EQ(0, st);
    ASSERT_EQ(5, s_start);
    ASSERT_EQ(0, s_asid);
    ASSERT_EQ(0x205u << 15, (uint32_t)((uintptr_t)r - ARCH_HOST_VA_BASE));
}

static void test_fixed_address_in_use(void)
{
    status_$t st;
    MST[4] = 1;
    mste(1, 6)->uid.high = 9;
    mst_$alloc_segs(6u << 15, &obj, 0, 0x8000, 0, 3, 5, 1, 0, 0, &info, &st);
    ASSERT_EQ(0x00040003, st);
    ASSERT_EQ(0, sets);
}

static void test_fixed_address_illegal(void)
{
    status_$t st;
    mst_$alloc_segs(0x150u << 15, &obj, 0, 0x8000, 0, 3, 5, 1, 0, 0, &info, &st);
    ASSERT_EQ(0x00040004, st);
    ASSERT_EQ(0, locks);
}

static void test_window_too_small(void)
{
    status_$t st;
    lk_len = 0x10000000;
    mst_$alloc_segs(0x7FFFFFFF, &obj, 0, 0x81u << 15, 0, 3, 5, 1, 0, 0, &info, &st);
    ASSERT_EQ(0x00040003, st);
}

static void test_offset_beyond_object(void)
{
    status_$t st;
    lk_len = 0x8000;
    mst_$alloc_segs(0x7FFFFFFF, &obj, 0x8000, 0x10, 0, 3, 5, 1, 0, 0, &info, &st);
    ASSERT_EQ(0x00040002, st);
    /* wired skips the length clamp */
    mst_$alloc_segs(0x7FFFFFFF, &obj, 0x8000, 0x10, 0, 3, 5, 1, 0xFF, 0, &info, &st);
    ASSERT_EQ(0, st);
    ASSERT_EQ(-1, s_wired);
}

static void test_lookup_failure(void)
{
    status_$t st;
    lk_status = 0x00040008;
    mst_$alloc_segs(0x7FFFFFFF, &obj, 0, 0x10, 0, 3, 5, 1, 0, 0, &info, &st);
    ASSERT_EQ(0x00040008, st);
    ASSERT_EQ(0, locks);
}

static void test_table_page_failure(void)
{
    status_$t st;
    alloc_status = 0x0004000E;
    mst_$alloc_segs(0x7FFFFFFF, &obj, 0, 0x10, 0, 3, 5, 1, 0, 0, &info, &st);
    ASSERT_EQ(0x0004000E, st);
    ASSERT_EQ(1, unlocks);
}

static void test_anonymous_area_threads_and_audits(void)
{
    status_$t st;
    uid_t anon = { 0xA0A0A0A0, 0x00070002 };   /* generation 7, area 2 */
    area_$entry_t *e = &mock_area_table[1];

    e->flags = AREA_FLAG_ACTIVE;
    e->generation = 7;
    e->virt_size = 0x40000;
    e->owner_asid = 9;                          /* not PROC1_$AS_ID */
    AUDIT_$ENABLED = -1;
    MST[5] = 1;
    mst_$alloc_segs(0x7FFFFFFF, &anon, 0, 0x10000, 0, 3, 0x16, 1, 0, 0, &info, &st);
    ASSERT_EQ(0, st);
    ASSERT_EQ(0, lookups);
    ASSERT_EQ(1, threads);
    ASSERT_EQ(0x00070002, th_handle);
    ASSERT_EQ(3, th_asid);
    ASSERT_EQ(0x7E, th_start);
    ASSERT_EQ((2u << 16) | 0, th_words);
    ASSERT_EQ(0, s_loc);
    ASSERT_EQ(1, audits);
    ASSERT_EQ(0x00040022, au_event.high);
    ASSERT_EQ(0, au_flag);
    ASSERT_EQ(2, au_data[0]);
    ASSERT_EQ(7, au_data[1]);
    ASSERT_EQ(9, au_data[2]);
    ASSERT_EQ(0x16, au_data[3]);

    /* owned by the current address space: no audit record */
    e->owner_asid = 3;
    mst_$alloc_segs(0x7FFFFFFF, &anon, 0, 0x8000, 0, 3, 0x16, 1, 0, 0, &info, &st);
    ASSERT_EQ(1, audits);
}

static void test_anonymous_bad_generation(void)
{
    status_$t st;
    uid_t anon = { 0xA0A0A0A0, 0x00080002 };
    mock_area_table[1].flags = AREA_FLAG_ACTIVE;
    mock_area_table[1].generation = 7;
    AUDIT_$ENABLED = -1;
    mst_$alloc_segs(0x7FFFFFFF, &anon, 0, 0x8000, 0, 3, 5, 1, 0, 0, &info, &st);
    ASSERT_EQ(0x00040001, st);
    ASSERT_EQ(1, audits);                     /* 0x40001 is always logged */
    ASSERT_EQ(1, au_flag);
}

int main(void)
{
    printf("mst_$alloc_segs tests:\n");
    RUN_TEST(zero_length);
    RUN_TEST(top_down_private);
    RUN_TEST(top_down_skips_used);
    RUN_TEST(bottom_up_global);
    RUN_TEST(fixed_global_address_forces_asid_0);
    RUN_TEST(fixed_address_in_use);
    RUN_TEST(fixed_address_illegal);
    RUN_TEST(window_too_small);
    RUN_TEST(offset_beyond_object);
    RUN_TEST(lookup_failure);
    RUN_TEST(table_page_failure);
    RUN_TEST(anonymous_area_threads_and_audits);
    RUN_TEST(anonymous_bad_generation);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
