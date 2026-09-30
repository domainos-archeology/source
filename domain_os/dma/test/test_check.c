/*
 * dma/test/test_check.c - DMA_$CHECK (0x00E0A3A6) against a register
 * arena standing in for the M68450 at SAU2_DMAC_BASE: the error-code
 * table, the active-channel abort and the transfer-count check.
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

#include "dma/dma.h"
#include "misc/misc.h"

static int n_crash;
static status_$t crash_st;
void CRASH_SYSTEM(const status_$t *status_p) { n_crash++; crash_st = *status_p; }

#include "../check.c"

static status_$t run(uint8_t csr, uint8_t cer, uint16_t mtc)
{
    memset(dmac, 0, sizeof dmac);
    dmac[0xC0] = csr; dmac[0xC1] = cer;
    dmac[0xCA] = (uint8_t)(mtc >> 8); dmac[0xCB] = (uint8_t)mtc;
    n_crash = 0;
    return DMA_$CHECK(3);
}

TEST(clean)
{
    ASSERT_EQ(0, run(0x80, 0, 0));
    ASSERT_EQ(0xFF, dmac[0xC0]);
    ASSERT_EQ(0, n_crash);
}

TEST(not_at_end)
{
    ASSERT_EQ(0x0008001D, run(0x00, 0, 0x0100));
    ASSERT_EQ(0x0008001D, run(0x08, 0, 0));
    ASSERT_EQ(0x10, dmac[0xC7]);            /* software abort */
    ASSERT_EQ(0xFF, dmac[0xC0]);
}

TEST(error_codes)
{
    ASSERT_EQ(0x00080017, run(0x10, 9, 0));
    ASSERT_EQ(0x00080017, run(0x10, 11, 0));
    ASSERT_EQ(0x00080006, run(0x10, 16, 0));
    ASSERT_EQ(0, n_crash);
    run(0x10, 1, 0);  ASSERT_EQ(1, n_crash); ASSERT_EQ(0x00080005, crash_st);
    run(0x10, 13, 0); ASSERT_EQ(0x00080005, crash_st);
    run(0x10, 8, 0);  ASSERT_EQ(0x00080019, crash_st);
    run(0x10, 12, 0); ASSERT_EQ(0x00080019, crash_st);
    run(0x10, 0, 0);  ASSERT_EQ(0x00080019, crash_st);
    run(0x10, 17, 0); ASSERT_EQ(0x00080019, crash_st);
}

TEST(channel_offset)
{
    memset(dmac, 0, sizeof dmac);
    dmac[0x40] = 0x08;
    ASSERT_EQ(0x0008001D, DMA_$CHECK(1));
    ASSERT_EQ(0x10, dmac[0x47]);
}

int main(void)
{
    printf("DMA_$CHECK tests:\n");
    RUN_TEST(clean);
    RUN_TEST(not_at_end);
    RUN_TEST(error_codes);
    RUN_TEST(channel_offset);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
