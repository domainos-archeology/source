/*
 * mst/test/test_map_at.c - MST_$MAP_AT (0x00E42F54): the twelve
 * arguments it hands mst_$alloc_segs.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    int _before = tests_failed; \
    printf("  Running %s... ", #name); \
    fflush(stdout); \
    test_##name(); \
    if (tests_failed == _before) { tests_passed++; printf("PASSED\n"); } \
} while (0)

#define ASSERT_EQ(expected, actual) do { \
    unsigned long _e = (unsigned long)(expected); \
    unsigned long _a = (unsigned long)(actual); \
    if (_e != _a) { \
        printf("FAILED\n    Expected: 0x%lx, Got: 0x%lx at line %d\n", \
               _e, _a, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while (0)

#include "mst/mst_internal.h"

uint16_t PROC1_$AS_ID;
uint16_t MST_$TOUCH_COUNT;

static uint32_t a_hint, a_start, a_len, a_size;
static uid_t *a_uid;
static int16_t a_asid;
static uint16_t a_area, a_touch;
static uint8_t a_rights;
static boolean a_dir;
static void *a_info;
static status_$t *a_status;

void *mst_$alloc_segs(uint32_t addr_hint, uid_t *uid, uint32_t start_va, uint32_t length,
                      uint32_t area_size, int16_t asid, uint16_t area_id, uint16_t touch_count,
                      uint8_t access_rights, boolean direction, void *map_info,
                      status_$t *status)
{
    a_hint = addr_hint; a_uid = uid; a_start = start_va; a_len = length;
    a_size = area_size; a_asid = asid; a_area = area_id; a_touch = touch_count;
    a_rights = access_rights; a_dir = direction; a_info = map_info; a_status = status;
    return (void *)0;
}

#include "../map_at.c"

TEST(forwards)
{
    uint32_t va = 0x00D40000u, start = 0x400u, len = 0x7FFFFFFFu, ext = 0x8000u;
    uint16_t mode = 3;
    uint8_t conc = 0xFF;
    uid_t u = { 9, 9 };
    uint32_t info;
    status_$t st;
    PROC1_$AS_ID = 2;
    MST_$TOUCH_COUNT = 4;
    MST_$MAP_AT(&va, &u, &start, &len, &mode, &ext, &conc, &info, &st);
    ASSERT_EQ(0x00D40000u, a_hint);
    ASSERT_EQ(1, a_uid == &u);
    ASSERT_EQ(0x400u, a_start);
    ASSERT_EQ(0x7FFFFFFFu, a_len);
    ASSERT_EQ(0x8000u, a_size);
    ASSERT_EQ(2, a_asid);
    ASSERT_EQ(3, a_area);
    ASSERT_EQ(4, a_touch);
    ASSERT_EQ(0xFF, a_rights);
    ASSERT_EQ(0, a_dir);
    ASSERT_EQ(1, a_info == (void *)&info);
    ASSERT_EQ(1, a_status == &st);
}

int main(void)
{
    printf("MST_$MAP_AT tests:\n");
    RUN_TEST(forwards);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
