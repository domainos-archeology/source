/*
 * net_io/test/test_boot_device.c - NET_IO_$BOOT_DEVICE (0x00E31C14)
 *
 * Pins the device classification (2/3 -> 0, 6 -> 4, 8 -> 5), the boot_unit
 * store on those arms only, the FALSE answer otherwise, and the two driver
 * network UIDs written on every call.
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

#include "net_io/net_io_internal.h"
#include "uid/uid.h"

net_io_unwired_t NET_IO_UNWIRED;
net_io_$driver_t NET_IO_$NIL_DRIVER[1];
net_io_$driver_t NET_IO_$USER_DRIVER[1];
uid_t NIL_$NETWORK_UID = { 0x702, 0 };
uid_t USER_$NETWORK_UID = { 0x704, 0 };

#include "../boot_device.c"

static void reset(void)
{
    memset(&NET_IO_UNWIRED, 0, sizeof(NET_IO_UNWIRED));
    NET_IO_UNWIRED.boot_unit = 0x3E7;
    NET_IO_UNWIRED.boot_port_type = 0x77;
    memset(NET_IO_$NIL_DRIVER, 0, sizeof(NET_IO_$NIL_DRIVER));
    memset(NET_IO_$USER_DRIVER, 0, sizeof(NET_IO_$USER_DRIVER));
}

TEST(device_2_and_3)
{
    reset();
    ASSERT_EQ(0xFF, (uint8_t)NET_IO_$BOOT_DEVICE(2, 0x12));
    ASSERT_EQ(0, NET_IO_UNWIRED.boot_port_type);
    ASSERT_EQ(0x12, NET_IO_UNWIRED.boot_unit);
    reset();
    ASSERT_EQ(0xFF, (uint8_t)NET_IO_$BOOT_DEVICE(3, 0x13));
    ASSERT_EQ(0, NET_IO_UNWIRED.boot_port_type);
    ASSERT_EQ(0x13, NET_IO_UNWIRED.boot_unit);
}

TEST(device_6_and_8)
{
    reset();
    ASSERT_EQ(0xFF, (uint8_t)NET_IO_$BOOT_DEVICE(6, 1));
    ASSERT_EQ(4, NET_IO_UNWIRED.boot_port_type);
    ASSERT_EQ(1, NET_IO_UNWIRED.boot_unit);
    reset();
    ASSERT_EQ(0xFF, (uint8_t)NET_IO_$BOOT_DEVICE(8, 2));
    ASSERT_EQ(5, NET_IO_UNWIRED.boot_port_type);
    ASSERT_EQ(2, NET_IO_UNWIRED.boot_unit);
}

TEST(disk_device_false_untouched)
{
    reset();
    ASSERT_EQ(0, (uint8_t)NET_IO_$BOOT_DEVICE(1, 5));
    ASSERT_EQ(0x77, NET_IO_UNWIRED.boot_port_type);
    ASSERT_EQ(0x3E7, NET_IO_UNWIRED.boot_unit);
    /* the UIDs are written regardless */
    ASSERT_EQ(0x702, NET_IO_$NIL_DRIVER[0].network_uid.high);
    ASSERT_EQ(0x704, NET_IO_$USER_DRIVER[0].network_uid.high);
}

int main(void)
{
    printf("NET_IO_$BOOT_DEVICE tests:\n");
    RUN_TEST(device_2_and_3);
    RUN_TEST(device_6_and_8);
    RUN_TEST(disk_device_false_untouched);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
