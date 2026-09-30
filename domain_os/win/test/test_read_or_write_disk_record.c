/*
 * win/test/test_read_or_write_disk_record.c - read_or_write_disk_record
 * (0x00E19414)
 *
 * Pins the DMAC channel-3 programming (MTC, MAR, BAR = length << 10, BTC,
 * OCR by operation, MFC, BFC, CCR), the per-operation counters at +0x40 /
 * +0x44, and the drive registers (sector, mode 9, go 1 or 2).
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

static uint8_t dmac[0x100] __attribute__((aligned(4)));
#define SAU2_DMAC_BASE ((uintptr_t)dmac)

#include "win/win_internal.h"

MODULE_DATA_DEFINE(win_$data_t, WIN_$DATA, 0x00E2B89C);
uint32_t win_$host_clockh(void) { return 0; }

#include "../read_or_write_disk_record.c"

static uint8_t regs[0x10];
static win_$request_t req;

static void reset(int8_t flags)
{
    memset(WIN_$DATA.bytes, 0, sizeof(WIN_$DATA.bytes));
    memset(regs, 0, sizeof(regs));
    memset(dmac, 0, sizeof(dmac));
    *(volatile uint8_t **)(WIN_UNIT(0) + WIN_BASE_ADDR_OFFSET) = regs;
    memset(&req, 0, sizeof(req));
    ARCH_HOST_VA_BASE = (uintptr_t)&req - 0x100;
    WIN_CUR_REQ_VA = ARCH_PTR_TO_VA(&req);
    req.pa = 0x12400;
    req.length = 0x3;
    req.sector = 0x11;
    req.flags = flags;
}

TEST(op2)
{
    reset(0x72);
    ASSERT_EQ(0, read_or_write_disk_record(0));
    ASSERT_EQ(0x10, *(uint16_t *)(dmac + 0xCA));
    ASSERT_EQ(0x12400, *(uint32_t *)(dmac + 0xCC));
    ASSERT_EQ(0x3u << 10, *(uint32_t *)(dmac + 0xDC));
    ASSERT_EQ(0x200, *(uint16_t *)(dmac + 0xDA));
    ASSERT_EQ(0x12, dmac[0xC5]);
    ASSERT_EQ(0, dmac[0xE9]);
    ASSERT_EQ(1, dmac[0xF9]);
    ASSERT_EQ(0xC0, dmac[0xC7]);
    ASSERT_EQ(1, *(uint32_t *)(WIN_DATA_BASE + 0x44));
    ASSERT_EQ(0, *(uint32_t *)(WIN_DATA_BASE + 0x40));
    ASSERT_EQ(0x11, regs[6]);
    ASSERT_EQ(9, regs[WIN_REG_MODE]);
    ASSERT_EQ(2, regs[WIN_REG_GO]);
}

TEST(other_op)
{
    reset(0x01);
    ASSERT_EQ(0, read_or_write_disk_record(0));
    ASSERT_EQ(0x92, dmac[0xC5]);
    ASSERT_EQ(1, *(uint32_t *)(WIN_DATA_BASE + 0x40));
    ASSERT_EQ(0, *(uint32_t *)(WIN_DATA_BASE + 0x44));
    ASSERT_EQ(1, regs[WIN_REG_GO]);
}

int main(void)
{
    printf("read_or_write_disk_record tests:\n");
    RUN_TEST(op2);
    RUN_TEST(other_op);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
