/*
 * disk/test/test_mnt_dinit.c - Unit tests for DISK_$MNT_DINIT (0x00E3DA64)
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

#include "../mnt_dinit.c"

static int calls;
static uint16_t got_unit, got_ctrl;
static void *got[5];

static void mock_dinit(uint16_t unit, uint16_t controller, void *a, void *b,
                       void *c, void *d, void *e)
{
    calls++;
    got_unit = unit; got_ctrl = controller;
    got[0] = a; got[1] = b; got[2] = c; got[3] = d; got[4] = e;
}

TEST(forwards_seven_arguments)
{
    disk_jump_table_t jt;
    disk_device_entry_t dev;
    void *devp = &dev;
    int a, b, c, d, e;
    memset(&jt, 0, sizeof jt);
    memset(&dev, 0, sizeof dev);
    jt.dinit = mock_dinit;
    dev.jump_table = &jt;
    dev.controller = 0x21;
    DISK_$MNT_DINIT(3, &devp, &a, &b, &c, &d, &e);
    ASSERT_EQ(1, calls);
    ASSERT_EQ(3, got_unit);
    ASSERT_EQ(0x21, got_ctrl);
    ASSERT_EQ((unsigned long)&a, (unsigned long)got[0]);
    ASSERT_EQ((unsigned long)&e, (unsigned long)got[4]);
}

int main(void)
{
    printf("test_mnt_dinit:\n");
    RUN_TEST(forwards_seven_arguments);
    printf("%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
