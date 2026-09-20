/*
 * disk/test/test_get_stats.c - Unit tests for DISK_$GET_STATS (0x00E3DB9C)
 *
 * disk/get_stats.c is #included below with a host copy of the DISK_$DEVICES
 * segment (32 entries plus the 22-byte statistics template at +0x180).
 */

#include <stdio.h>
#include <string.h>

#include "disk/disk_internal.h"

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    printf("  Running %s... ", #name); \
    test_##name(); \
    tests_passed++; \
    printf("PASSED\n"); \
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

/* The host descriptor is wider than 0x0c, so the template is placed at
 * DISK_MAX_DEVICES * sizeof(entry) + the offset the code adds; on the
 * target both are 0x180. */
static uint8_t devices_segment[sizeof(disk_device_entry_t) * DISK_MAX_DEVICES + 0x200];
#define DISK_$DEVICES ((disk_device_entry_t *)devices_segment)
#undef DISK_DEVICES_STATS_OFFSET
#define DISK_DEVICES_STATS_OFFSET (sizeof(disk_device_entry_t) * DISK_MAX_DEVICES)

#include "../get_stats.c"

static int stats_calls;
static uint16_t stats_cnum, stats_unit;
static uint32_t stats_fill0, stats_fill1;

static void mock_get_stats(uint16_t cnum, uint16_t unit, void *stats)
{
    stats_calls++;
    stats_cnum = cnum;
    stats_unit = unit;
    ((uint32_t *)stats)[0] = stats_fill0;
    ((uint32_t *)stats)[1] = stats_fill1;
}

static disk_jump_table_t jt_with, jt_without;

static void reset(void)
{
    uint32_t *tmpl = (uint32_t *)(devices_segment + DISK_DEVICES_STATS_OFFSET);
    memset(devices_segment, 0, sizeof devices_segment);
    tmpl[0] = 0x01010101; tmpl[1] = 0x02020202; tmpl[2] = 0x03030303;
    tmpl[3] = 0x04040404; tmpl[4] = 0x05050505;
    *(uint16_t *)&tmpl[5] = 0x0606;
    *(uint16_t *)((uint8_t *)&tmpl[5] + 2) = 0x7777;    /* beyond the copy */
    memset(&jt_with, 0, sizeof jt_with);
    memset(&jt_without, 0, sizeof jt_without);
    jt_with.get_stats = mock_get_stats;
    stats_calls = 0;
    stats_fill0 = stats_fill1 = 0;
}

static void reg(int i, disk_jump_table_t *jt, uint16_t type, uint16_t ctrl)
{
    DISK_$DEVICES[i].jump_table = jt;
    DISK_$DEVICES[i].device_type = type;
    DISK_$DEVICES[i].controller = ctrl;
}

TEST(template_copied_and_no_driver)
{
    uint8_t has = 0x55;
    uint32_t buf[6];
    reset();
    memset(buf, 0xee, sizeof buf);
    DISK_$GET_STATS(1, 0, 0, &has, buf);
    ASSERT_EQ(0, has);
    ASSERT_EQ(0x01010101, buf[0]);
    ASSERT_EQ(0x05050505, buf[4]);
    ASSERT_EQ(0x0606, *(uint16_t *)&buf[5]);
    ASSERT_EQ(0xeeee, *(uint16_t *)((uint8_t *)&buf[5] + 2));   /* 22 bytes only */
}

TEST(driver_called_and_flag_set)
{
    uint8_t has = 0;
    uint32_t buf[6];
    reset();
    reg(4, &jt_with, 3, 1);
    stats_fill1 = 5;
    DISK_$GET_STATS(3, 1, 9, &has, buf);
    ASSERT_EQ(1, stats_calls);
    ASSERT_EQ(1, stats_cnum);
    ASSERT_EQ(9, stats_unit);
    ASSERT_EQ(0xff, has);
}

TEST(driver_returns_zeros_flag_clear)
{
    uint8_t has = 0;
    uint32_t buf[6];
    reset();
    reg(0, &jt_with, 3, 1);
    DISK_$GET_STATS(3, 1, 0, &has, buf);
    ASSERT_EQ(1, stats_calls);
    ASSERT_EQ(0, has);
}

/* A matching entry with a null slot ends the search: a later match is
 * not consulted. */
TEST(null_slot_stops_search)
{
    uint8_t has = 0;
    uint32_t buf[6];
    reset();
    reg(2, &jt_without, 3, 1);
    reg(3, &jt_with, 3, 1);
    stats_fill0 = 1;
    DISK_$GET_STATS(3, 1, 0, &has, buf);
    ASSERT_EQ(0, stats_calls);
    ASSERT_EQ(0, has);
}

TEST(unregistered_and_mismatched_entries_skipped)
{
    uint8_t has = 0;
    uint32_t buf[6];
    reset();
    reg(1, NULL, 3, 1);
    reg(2, &jt_with, 3, 2);
    reg(31, &jt_with, 3, 1);
    stats_fill0 = 1;
    DISK_$GET_STATS(3, 1, 0, &has, buf);
    ASSERT_EQ(1, stats_calls);
    ASSERT_EQ(0xff, has);
}

int main(void)
{
    printf("test_get_stats:\n");
    RUN_TEST(template_copied_and_no_driver);
    RUN_TEST(driver_called_and_flag_set);
    RUN_TEST(driver_returns_zeros_flag_clear);
    RUN_TEST(null_slot_stops_search);
    RUN_TEST(unregistered_and_mismatched_entries_skipped);
    printf("%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
