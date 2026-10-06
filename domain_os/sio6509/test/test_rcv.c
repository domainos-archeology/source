/*
 * sio6509/test/test_rcv.c - SIO6509_$RCV (0x00E1D53E): the status bit 7
 * gate, the data register, and the error-flag table index.
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

#include "sio6509/sio6509_internal.h"

sio2681_global_data_t SIO2681_$DATA;

static int rcv_calls;
static sio_desc_t *rcv_desc;
static uint8_t rcv_char;
static uint32_t rcv_flags;
void SIO_$I_RCV(sio_desc_t *desc, uint8_t c, uint32_t flags)
{
    rcv_calls++;
    rcv_desc = desc;
    rcv_char = c;
    rcv_flags = flags;
}

#include "../rcv.c"

/* VA 0 = arena: the channel record at 0x10, registers at 0x40, desc at 0x80 */
static uint8_t arena[0x100] __attribute__((aligned(4)));

static sio6509_chan_t *setup(uint8_t status, uint8_t data)
{
    sio6509_chan_t *c = (sio6509_chan_t *)&arena[0x10];
    static const uint32_t flags[8] = { 0, 2, 4, 6, 1, 5, 3, 0 };
    memset(arena, 0, sizeof(arena));
    memcpy(SIO2681_$DATA.sio6509_rcv_flags, flags, sizeof(flags));
    ARCH_HOST_VA_BASE = (uintptr_t)arena;
    c->regs = 0x40;
    c->sio_desc = 0x80;
    arena[0x41] = status;
    arena[0x43] = data;
    rcv_calls = 0;
    return c;
}

TEST(no_character)
{
    setup(0x7F, 'x');
    SIO6509_$RCV(0x10);
    ASSERT_EQ(0, rcv_calls);
}

TEST(character_with_flags)
{
    setup(0x80 | 0x30, 'q');
    SIO6509_$RCV(0x10);
    ASSERT_EQ(1, rcv_calls);
    ASSERT_EQ('q', rcv_char);
    ASSERT_EQ((uintptr_t)&arena[0x80], (uintptr_t)rcv_desc);
    ASSERT_EQ(6, rcv_flags);                    /* index 3 */
    setup(0xF0, 'r');
    SIO6509_$RCV(0x10);
    ASSERT_EQ(0, rcv_flags);                    /* index 7 */
    setup(0xC0, 'r');
    SIO6509_$RCV(0x10);
    ASSERT_EQ(1, rcv_flags);                    /* index 4 */
}

int main(void)
{
    printf("SIO6509_$RCV\n");
    RUN_TEST(no_character);
    RUN_TEST(character_with_flags);
    printf("%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
