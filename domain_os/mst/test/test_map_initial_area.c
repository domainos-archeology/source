/*
 * mst/test/test_map_initial_area.c - unit tests for MST_$MAP_INITIAL_AREA (0x00E42E9E).
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
uint16_t MST_$TOUCH_COUNT = 4;
uid_t ANON_$UID = { 0xA0A00000u, 0 };

static status_$t xfer_st, alloc_st;
static int n_xfer, n_alloc, n_unmap;
static area_$handle_t *x_handle;
static int16_t x_asid, x_seg;
static uint32_t x_size;
static uint32_t a_hint, a_start, a_len;
static int16_t a_asid;
static uint16_t a_area, a_touch;
static uid_t *a_uid;
static int16_t u_mode;
static uid_t *u_uid;
static uint32_t u_start, u_size;
static uint16_t u_asid;
static status_$t *u_status;

int16_t AREA_$TRANSFER(area_$handle_t *handle_ptr, int16_t new_asid,
                       int16_t new_seg_idx, uint32_t new_virt_size,
                       status_$t *status_ret)
{
    n_xfer++; x_handle = handle_ptr; x_asid = new_asid; x_seg = new_seg_idx;
    x_size = new_virt_size;
    *status_ret = xfer_st;
    return 0x21;
}
void *mst_$alloc_segs(uint32_t addr_hint, uid_t *uid, uint32_t start_va, uint32_t length,
                      uint32_t area_size, int16_t asid, uint16_t area_id, uint16_t touch_count,
                      uint8_t access_rights, boolean direction, void *map_info,
                      status_$t *status)
{
    (void)area_size; (void)access_rights; (void)direction; (void)map_info;
    n_alloc++; a_hint = addr_hint; a_uid = uid; a_start = start_va; a_len = length;
    a_asid = asid; a_area = area_id; a_touch = touch_count;
    *status = alloc_st;
    return NULL;
}
void MST_$UNMAP_PRIVI(int16_t mode, uid_t *uid, uint32_t start, uint32_t size,
                      uint16_t asid, status_$t *status)
{
    n_unmap++; u_mode = mode; u_uid = uid; u_start = start; u_size = size;
    u_asid = asid; u_status = status; *status = 0;
}

#include "../map_initial_area.c"

static uid_t uid;
static status_$t st;

static void reset(void)
{
    xfer_st = alloc_st = 0;
    n_xfer = n_alloc = n_unmap = 0;
    uid.high = 0xA0A00000u; uid.low = 0x00020005u;
    st = 0x1234;
}

TEST(not_an_area)
{
    reset(); uid.high = 0x12345678u;
    MST_$MAP_INITIAL_AREA(0x8000, 3, &uid, 0x10000, 7, false, &st);
    ASSERT_EQ(status_$mst_uid_not_vm_area, st);
    ASSERT_EQ(0, n_xfer);
}

TEST(maps_without_touch)
{
    reset();
    MST_$MAP_INITIAL_AREA(0x00318000u, 3, &uid, 0x10000, 7, false, &st);
    ASSERT_EQ(status_$ok, st);
    ASSERT_EQ((uintptr_t)&uid.low, (uintptr_t)x_handle);
    ASSERT_EQ(3, x_asid);
    ASSERT_EQ(0x63, x_seg);
    ASSERT_EQ(0x10000, x_size);
    ASSERT_EQ(0x00318000u, a_hint);
    ASSERT_EQ((uintptr_t)&uid, (uintptr_t)a_uid);
    ASSERT_EQ(0, a_start);
    ASSERT_EQ(0x10000, a_len);
    ASSERT_EQ(3, a_asid);
    ASSERT_EQ(7, a_area);
    ASSERT_EQ(4, a_touch);
    ASSERT_EQ(0, n_unmap);
}

/* touch: unmap the previous segment in the current ASID, even after a
 * failed map. */
TEST(touch_unmaps_old_segment)
{
    reset(); alloc_st = 0x00040003;
    MST_$MAP_INITIAL_AREA(0x00318000u, 3, &uid, 0x10000, 7, true, &st);
    ASSERT_EQ(0x00040003, st);
    ASSERT_EQ(1, n_unmap);
    ASSERT_EQ(0, u_mode);
    ASSERT_EQ((uintptr_t)&uid, (uintptr_t)u_uid);
    ASSERT_EQ(0x21u << 15, u_start);
    ASSERT_EQ(0x8000, u_size);
    ASSERT_EQ(6, u_asid);
    ASSERT_EQ(1, u_status != &st);
}

TEST(transfer_fails)
{
    reset(); xfer_st = 0x00300002;
    MST_$MAP_INITIAL_AREA(0, 3, &uid, 0, 7, true, &st);
    ASSERT_EQ(0x00300002, st);
    ASSERT_EQ(0, n_alloc);
    ASSERT_EQ(0, n_unmap);
}

int main(void)
{
    printf("MST_$MAP_INITIAL_AREA tests\n");
    RUN_TEST(not_an_area);
    RUN_TEST(maps_without_touch);
    RUN_TEST(touch_unmaps_old_segment);
    RUN_TEST(transfer_fails);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
