/*
 * disk/test/test_read_mfg_badspots.c - Unit tests for
 * DISK_$READ_MFG_BADSPOTS (0x00E6B7E4)
 */

#include <stdio.h>
#include <string.h>

#include "disk/disk_internal.h"
#include "wp/wp.h"

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

static status_$t setup_status;
static uint32_t setup_buffer;
static status_$t io_result;
static uint16_t io_op, io_vol;
static uint32_t io_ppn, io_daddr, io_info0;
static int unwires;
static uint32_t unwired;

uint32_t AS_IO_SETUP(uint16_t *vol_idx_ptr, uint32_t buffer, status_$t *status)
{
    (void)vol_idx_ptr;
    setup_buffer = buffer;
    *status = setup_status;
    return 0xABC00;
}

status_$t DISK_IO(uint16_t op, uint16_t vol_idx, uint32_t ppn, uint32_t daddr,
                  uint32_t *info)
{
    io_op = op; io_vol = vol_idx; io_ppn = ppn; io_daddr = daddr; io_info0 = info[0];
    return io_result;
}

void WP_$UNWIRE(uint32_t w) { unwires++; unwired = w; }

#include "../read_mfg_badspots.c"

static void reset(void)
{
    setup_status = 0; io_result = 0; unwires = 0;
}

TEST(reads_and_unwires)
{
    uint16_t v = 3;
    uint32_t d = 0x777;
    status_$t st = 0;
    reset();
    io_result = 0x55;
    DISK_$READ_MFG_BADSPOTS(&v, &d, 0x20000, &st);
    ASSERT_EQ(0x20000, setup_buffer);
    ASSERT_EQ(4, io_op);
    ASSERT_EQ(3, io_vol);
    ASSERT_EQ(0xABC00, io_ppn);
    ASSERT_EQ(0x777, io_daddr);
    ASSERT_EQ(0, io_info0);
    ASSERT_EQ(0x55, st);
    ASSERT_EQ(1, unwires);
    ASSERT_EQ(0xABC00, unwired);
}

TEST(header_error_forgiven)
{
    uint16_t v = 3;
    uint32_t d = 1;
    status_$t st = 0;
    reset();
    io_result = status_$disk_block_header_error;
    DISK_$READ_MFG_BADSPOTS(&v, &d, 0x20000, &st);
    ASSERT_EQ(0, st);
}

/* A setup failure is a LOW-word test: a subsystem-only code passes. */
TEST(setup_failure_low_word)
{
    uint16_t v = 3;
    uint32_t d = 1;
    status_$t st = 0;
    reset();
    setup_status = 0x0008000F;
    DISK_$READ_MFG_BADSPOTS(&v, &d, 0x20000, &st);
    ASSERT_EQ(0x0008000F, st);
    ASSERT_EQ(0, unwires);
    setup_status = 0x00080000;
    io_result = 0;
    DISK_$READ_MFG_BADSPOTS(&v, &d, 0x20000, &st);
    ASSERT_EQ(1, unwires);
}

int main(void)
{
    printf("test_read_mfg_badspots:\n");
    RUN_TEST(reads_and_unwires);
    RUN_TEST(header_error_forgiven);
    RUN_TEST(setup_failure_low_word);
    printf("%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
