/*
 * ring/test/test_poll_sticky_bpherr.c - RING_$POLL_STICKY_BPHERR (0x00E76290)
 *
 * Pins: a non-zero DCTE cstatus answers FALSE and leaves the register alone;
 * otherwise bit 13 of the mode word gates bit 12 as the answer and the word
 * is written zero on both inner arms.
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

#include "../poll_sticky_bpherr.c"

static struct {
    dcte_t dcte;
    ring_hw_regs_t regs;
} arena;
static uint32_t cell;

static void setup(uint16_t mode, status_$t cstatus)
{
    memset(&arena, 0, sizeof(arena));
    ARCH_HOST_VA_BASE = (uintptr_t)&arena - 0x1000;
    arena.dcte.cstatus = cstatus;
    arena.dcte.disk_dinit = ARCH_PTR_TO_VA(&arena.regs);
    arena.regs.mode = mode;
    cell = ARCH_PTR_TO_VA(&arena.dcte);
}

TEST(bad_status_false_untouched)
{
    setup(0x3000, 0x00100002);
    ASSERT_EQ(0, (uint8_t)RING_$POLL_STICKY_BPHERR(&cell));
    ASSERT_EQ(0x3000, arena.regs.mode);
}

TEST(both_bits_true_and_cleared)
{
    setup(0x3000, 0);
    ASSERT_EQ(0xFF, (uint8_t)RING_$POLL_STICKY_BPHERR(&cell));
    ASSERT_EQ(0, arena.regs.mode);
}

TEST(bit13_only_false_and_cleared)
{
    setup(0x2000, 0);
    ASSERT_EQ(0, (uint8_t)RING_$POLL_STICKY_BPHERR(&cell));
    ASSERT_EQ(0, arena.regs.mode);
}

TEST(bit12_without_bit13_false_and_cleared)
{
    setup(0x1000, 0);
    ASSERT_EQ(0, (uint8_t)RING_$POLL_STICKY_BPHERR(&cell));
    ASSERT_EQ(0, arena.regs.mode);
}

int main(void)
{
    printf("RING_$POLL_STICKY_BPHERR tests:\n");
    RUN_TEST(bad_status_false_untouched);
    RUN_TEST(both_bits_true_and_cleared);
    RUN_TEST(bit13_only_false_and_cleared);
    RUN_TEST(bit12_without_bit13_false_and_cleared);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
