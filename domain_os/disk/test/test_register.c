/*
 * disk/test/test_register.c - Unit tests for DISK_$REGISTER (0x00E3D9AC)
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

disk_device_entry_t DISK_$DEVICES[DISK_MAX_DEVICES];

#include "../register.c"

static void dummy(void) {}
static disk_jump_table_t jt;

static uint8_t reg(uint16_t type, uint16_t ctrl, uint16_t units, uint16_t flags)
{
    void *p = &jt;
    return DISK_$REGISTER(&type, &ctrl, &units, &flags, &p);
}

static void reset(void)
{
    memset(DISK_$DEVICES, 0, sizeof DISK_$DEVICES);
    memset(&jt, 0, sizeof jt);
    jt.dinit = (void (*)(uint16_t, uint16_t, void *, void *, void *, void *, void *))dummy;
    jt.do_io = (void (*)(void *, void *, void *, void *))dummy;
}

TEST(stores_in_first_free_slot)
{
    reset();
    DISK_$DEVICES[0].jump_table = &jt;
    ASSERT_EQ(0xFF, reg(3, 1, 0x200, 7));
    ASSERT_EQ((unsigned long)&jt, (unsigned long)DISK_$DEVICES[1].jump_table);
    ASSERT_EQ(3, DISK_$DEVICES[1].device_type);
    ASSERT_EQ(1, DISK_$DEVICES[1].controller);
    ASSERT_EQ(0x200, DISK_$DEVICES[1].unit_count);
    ASSERT_EQ(7, DISK_$DEVICES[1].flags);
    ASSERT_EQ(0, (unsigned long)DISK_$DEVICES[2].jump_table);
}

TEST(vector_without_dinit_rejected)
{
    reset();
    jt.dinit = NULL;
    ASSERT_EQ(0, reg(3, 1, 0, 0));
    ASSERT_EQ(0, (unsigned long)DISK_$DEVICES[0].jump_table);
}

TEST(vector_without_do_io_rejected)
{
    reset();
    jt.do_io = NULL;
    ASSERT_EQ(0, reg(3, 1, 0, 0));
}

TEST(full_table_rejected)
{
    int i;
    reset();
    for (i = 0; i < DISK_MAX_DEVICES; i++) DISK_$DEVICES[i].jump_table = &jt;
    ASSERT_EQ(0, reg(3, 1, 0, 0));
}

int main(void)
{
    printf("test_register:\n");
    RUN_TEST(stores_in_first_free_slot);
    RUN_TEST(vector_without_dinit_rejected);
    RUN_TEST(vector_without_do_io_rejected);
    RUN_TEST(full_table_rejected);
    printf("%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
