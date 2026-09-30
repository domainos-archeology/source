/*
 * sio6509/test/test_init.c - SIO6509_$INIT (0x00E3350C): the PTRS entry,
 * the exception vector, the channel record and the two configuration
 * bytes, against a register arena at SAU2_SIO_BASE.
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

/* One arena for the SIO page and the channel record, VA 0xFFB000 = arena */
static uint8_t arena[0x200] __attribute__((aligned(4)));
#define SAU2_SIO_BASE 0x00FFB000u

#include "sio6509/sio6509_internal.h"

void *arch_$vector_table[ARCH_VECTOR_COUNT];
m68k_ptr_t SIO6509_$PTRS[SIO6509_PTRS_COUNT];
void SIO6509_$INT1_RTE(void) { }

#include "../init.c"

TEST(term_init_arguments)
{
    int16_t vec = 1, chip = 2;
    m68k_ptr_t desc = 0x00E2DC80u;
    uint8_t config[2] = { 0x03, 0xD9 };
    sio6509_chan_t *chan = (sio6509_chan_t *)(arena + 0x100);
    ARCH_HOST_VA_BASE = (uintptr_t)arena - 0x00FFB000u;
    memset(arena, 0, sizeof arena);
    SIO6509_$INIT(&vec, &chip, chan, &desc, config);
    ASSERT_EQ(0x00FFB100u, SIO6509_$PTRS[0]);
    ASSERT_EQ(1, arch_$vector_table[26] == (void *)SIO6509_$INT1_RTE);
    ASSERT_EQ(0x00FFB020u, chan->regs);
    ASSERT_EQ(0x00E2DC80u, chan->sio_desc);
    ASSERT_EQ(0xD9, arena[0x21]);           /* the last byte written wins */
}

TEST(chip_one_vector_two)
{
    int16_t vec = 2, chip = 1;
    m68k_ptr_t desc = 7;
    uint8_t config[2] = { 0x11, 0x22 };
    sio6509_chan_t *chan = (sio6509_chan_t *)(arena + 0x180);
    ARCH_HOST_VA_BASE = (uintptr_t)arena - 0x00FFB000u;
    SIO6509_$INIT(&vec, &chip, chan, &desc, config);
    ASSERT_EQ(0x00FFB180u, SIO6509_$PTRS[1]);
    ASSERT_EQ(1, arch_$vector_table[27] == (void *)SIO6509_$INT1_RTE);
    ASSERT_EQ(0x00FFB000u, chan->regs);
    ASSERT_EQ(0x22, arena[0x01]);
}

int main(void)
{
    printf("SIO6509_$INIT tests:\n");
    RUN_TEST(term_init_arguments);
    RUN_TEST(chip_one_vector_two);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
