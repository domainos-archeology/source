/*
 * disk/test/test_diskless_init.c - unit tests for disk_$diskless_init
 * (0x00E6B6DC)
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

#include "disk/disk_internal.h"
#include "mst/mst.h"
#include "network/network.h"
#include "dbuf/dbuf.h"

int8_t NETWORK_$REALLY_DISKLESS;
static char log_buf[64];
static void note(const char *s) { strcat(log_buf, s); }
static uint32_t ranges[4];
static int n_wire;

void MST_$WIRE_AREA(const void *start_va_ptr, const void *end_va_ptr,
                    void *page_list, const void *max_pages_ptr,
                    void *page_count_ret)
{
    (void)page_list;
    note("M");
    ranges[n_wire * 2] = *(const uint32_t *)start_va_ptr;
    ranges[n_wire * 2 + 1] = *(const uint32_t *)end_va_ptr;
    n_wire++;
    if (*(const uint16_t *)max_pages_ptr != 0x20) note("!");
    *(uint16_t *)page_count_ret = 1;
}
void DBUF_$INIT(void) { note("B"); }
void DISK_$INIT(void) { note("D"); }

#include "../diskless_init.c"

static void reset_state(void)
{
    log_buf[0] = 0;
    n_wire = 0;
    memset(ranges, 0, sizeof(ranges));
}

static void test_diskless_wires_once(void)
{
    NETWORK_$REALLY_DISKLESS = (int8_t)0xFF;
    disk_$diskless_init();
    ASSERT_EQ(0, strcmp(log_buf, "MMBD"));
    ASSERT_EQ(0x00E3824C, ranges[0]);
    ASSERT_EQ(0x00E3E746, ranges[1]);
    ASSERT_EQ(0x00E784D0, ranges[2]);
    ASSERT_EQ(0x00E7B044, ranges[3]);
    ASSERT_EQ(0, NETWORK_$REALLY_DISKLESS);
    disk_$diskless_init();
    ASSERT_EQ(0, strcmp(log_buf, "MMBD"));
}

static void test_disked_node_does_nothing(void)
{
    NETWORK_$REALLY_DISKLESS = 0;
    disk_$diskless_init();
    ASSERT_EQ(0, strlen(log_buf));
}

int main(void)
{
    printf("disk_$diskless_init tests\n");
    RUN_TEST(diskless_wires_once);
    RUN_TEST(disked_node_does_nothing);
    printf("%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
