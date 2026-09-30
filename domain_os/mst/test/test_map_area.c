/*
 * mst/test/test_map_area.c - unit tests for MST_$MAP_AREA (0x00E43B3A).
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
#include "anon/anon.h"

uint16_t PROC1_$AS_ID = 6;
uid_t ANON_$UID = { 0xA0A00000u, 0 };

static status_$t create_st, alloc_st;
static int n_create, n_alloc, n_delete;
static uint32_t c_virt, c_commit, a_hint, a_start, a_len, a_size;
static boolean c_rev, a_dir;
static uid_t a_uid;
static int16_t a_asid;
static uint16_t a_area, a_touch;
static uint8_t a_access;
static area_$handle_t d_handle;
static char mapped;

area_$handle_t AREA_$CREATE(uint32_t virt_size, uint32_t commit_size,
                            boolean shared, status_$t *status_p)
{
    n_create++; c_virt = virt_size; c_commit = commit_size; c_rev = shared;
    *status_p = create_st;
    return 0x00050003u;
}
void AREA_$DELETE(area_$handle_t handle, status_$t *st)
{
    n_delete++; d_handle = handle; *st = 0;
}
void *mst_$alloc_segs(uint32_t addr_hint, uid_t *uid, uint32_t start_va, uint32_t length,
                      uint32_t area_size, int16_t asid, uint16_t area_id, uint16_t touch_count,
                      uint8_t access_rights, boolean direction, void *map_info,
                      status_$t *status)
{
    (void)map_info;
    n_alloc++; a_hint = addr_hint; a_uid = *uid; a_start = start_va; a_len = length;
    a_size = area_size; a_asid = asid; a_area = area_id; a_touch = touch_count;
    a_access = access_rights; a_dir = direction;
    *status = alloc_st;
    return &mapped;
}

#include "../map_area.c"

static uint32_t virt, commit;
static boolean rev;
static uid_t out;
static status_$t st;

static void reset(void)
{
    create_st = alloc_st = 0;
    n_create = n_alloc = n_delete = 0;
    virt = 0x20000; commit = 0x400; rev = false;
    out.high = out.low = 0xEEEEEEEEu;
}

TEST(maps_forward_area)
{
    void *r;
    reset();
    r = MST_$MAP_AREA(&virt, &commit, &rev, &out, &st);
    ASSERT_EQ((uintptr_t)&mapped, (uintptr_t)r);
    ASSERT_EQ(0x20000, c_virt);
    ASSERT_EQ(0x400, c_commit);
    ASSERT_EQ(0x7FFFFFFFu, a_hint);
    ASSERT_EQ(0xA0A00000u, a_uid.high);
    ASSERT_EQ(0x00050003u, a_uid.low);
    ASSERT_EQ(0, a_start);
    ASSERT_EQ(0x20000, a_len);
    ASSERT_EQ(0, a_size);
    ASSERT_EQ(6, a_asid);
    ASSERT_EQ(7, a_area);
    ASSERT_EQ(1, a_touch);
    ASSERT_EQ(0, a_access);
    ASSERT_EQ(0, (uint8_t)a_dir);
    ASSERT_EQ(0x00050003u, out.low);
    ASSERT_EQ(0, n_delete);
}

TEST(reversed_offset)
{
    reset(); rev = true;
    MST_$MAP_AREA(&virt, &commit, &rev, &out, &st);
    ASSERT_EQ(0xFF, (uint8_t)c_rev);
    ASSERT_EQ(0x7FFFFFFFu - 0x1FFFFu, a_start);
}

TEST(map_fails_deletes_area)
{
    void *r;
    reset(); alloc_st = 0x00040003;
    r = MST_$MAP_AREA(&virt, &commit, &rev, &out, &st);
    ASSERT_EQ(0x00040003, st);
    ASSERT_EQ(1, n_delete);
    ASSERT_EQ(0x00050003u, d_handle);
    ASSERT_EQ(0xEEEEEEEEu, out.low);
    ASSERT_EQ((uintptr_t)&mapped, (uintptr_t)r);
}

/* A failed create returns at once (the A0 result is indeterminate). */
TEST(create_fails)
{
    reset(); create_st = 0x00300001;
    MST_$MAP_AREA(&virt, &commit, &rev, &out, &st);
    ASSERT_EQ(0x00300001, st);
    ASSERT_EQ(0, n_alloc);
    ASSERT_EQ(0xEEEEEEEEu, out.low);
}

int main(void)
{
    printf("MST_$MAP_AREA tests\n");
    RUN_TEST(maps_forward_area);
    RUN_TEST(reversed_offset);
    RUN_TEST(map_fails_deletes_area);
    RUN_TEST(create_fails);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
