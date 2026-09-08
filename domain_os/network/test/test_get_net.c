/*
 * network/test/test_get_net.c - unit tests for NETWORK_$GET_NET (0x00E0F2CC)
 *
 * The point of these tests is bead source-9orn: "and.w (0x8,A6),D0w" at
 * 0x00E0F2E4 masks the HIGH word of the longword first argument, so the table
 * index is bits 4..9 of net_addr >> 16, not bits 4..9 of net_addr.  The tree
 * used to mask the low half, which picked slot 0 (and therefore the "no
 * network" arm) for every real address.
 *
 * That the argument is a longword is settled by the only call site: at
 * 0x00E021FA ast_$force_activate_segment does "move.l (0xc,A6),-(SP)" - the
 * same longword whose low 20 bits it masks off at 0x00E021E6.
 */

#include <stdio.h>
#include <string.h>

#include "network/network_internal.h"

/* ============================================================================
 * Test framework
 * ============================================================================ */

static int tests_run = 0;
static int tests_failed = 0;
static int current_failed = 0;

#define TEST(name)      static void test_##name(void)
#define RUN_TEST(name)  do {                                                  \
        printf("  %-46s ", #name);                                            \
        current_failed = 0;                                                   \
        tests_run++;                                                          \
        test_##name();                                                        \
        if (current_failed == 0) { printf("PASSED\n"); }                      \
    } while (0)

#define ASSERT_EQ(expected, actual) do {                                      \
        unsigned long _e = (unsigned long)(expected);                         \
        unsigned long _a = (unsigned long)(actual);                           \
        if (_e != _a) {                                                       \
            if (current_failed == 0) { printf("FAILED\n"); }                  \
            printf("      line %d: expected 0x%lx, got 0x%lx\n",              \
                   __LINE__, _e, _a);                                         \
            current_failed = 1; tests_failed++;                               \
            return;                                                           \
        }                                                                     \
    } while (0)

/* ============================================================================
 * Globals the code under test references
 * ============================================================================ */

network_table_entry_t NETWORK_$NET_TABLE[NETWORK_TABLE_SIZE];

/* ============================================================================
 * The code under test
 * ============================================================================ */

#include "../get_net.c"

/* ============================================================================
 * Fixtures
 * ============================================================================ */

/* Build a longword whose high word carries `index` in bits 4..9, plus low
 * bits that must be ignored. */
static uint32_t addr_with_index(uint16_t index, uint32_t noise)
{
    return ((uint32_t)((index & 0x3F) << 4) << 16) | (noise & 0x000FFFFFu);
}

static void reset(void)
{
    memset(NETWORK_$NET_TABLE, 0, sizeof(NETWORK_$NET_TABLE));
}

/* ============================================================================
 * Tests
 * ============================================================================ */

TEST(index_comes_from_the_high_word)
{
    /* Slot 7 selected by bits 20..25; the low 20 bits are the node id and
     * must not reach the index. */
    uint32_t out = 0xDEADBEEFu;
    status_$t st = 0x7FFFFFFF;

    reset();
    NETWORK_$NET_TABLE[7].net_id = 0xAABBCCDDu;

    NETWORK_$GET_NET(addr_with_index(7, 0x000FFFFFu), &out, &st);

    ASSERT_EQ(0xAABBCCDDu, out);
    ASSERT_EQ(status_$ok, st);
}

TEST(low_word_bits_are_ignored)
{
    /*
     * The regression this test exists for: 0x03F00000 (mask 0x3F0 in the HIGH
     * word, bits 20..25 of the longword) names slot 0x3F, and the old low-half
     * mask read it as slot 0.  Conversely 0x000003F0 - which the old code read
     * as slot 0x3F - names slot 0.
     */
    uint32_t out = 0xDEADBEEFu;
    status_$t st = 0x7FFFFFFF;

    reset();
    NETWORK_$NET_TABLE[0x3F].net_id = 0x12345678u;

    /* Index in the high word: slot 0x3F. */
    NETWORK_$GET_NET(0x03F00000u, &out, &st);
    ASSERT_EQ(0x12345678u, out);
    ASSERT_EQ(status_$ok, st);

    /* The same bit pattern in the LOW word means index 0 - the special
     * "no network" arm at 0x00E0F2EC. */
    out = 0xDEADBEEFu;
    st = 0x7FFFFFFF;
    NETWORK_$GET_NET(0x000003F0u, &out, &st);
    ASSERT_EQ(0, out);
    ASSERT_EQ(status_$ok, st);
}

TEST(index_zero_yields_zero_and_ok)
{
    /* "bne.b" at 0x00E0F2EA falls through to "clr.l (A0)" and the ok exit. */
    uint32_t out = 0xDEADBEEFu;
    status_$t st = 0x7FFFFFFF;

    reset();
    NETWORK_$NET_TABLE[0].net_id = 0x99999999u;   /* slot 0 is never read */

    NETWORK_$GET_NET(0xFFF0FFFFu & ~0x03F00000u, &out, &st);

    ASSERT_EQ(0, out);
    ASSERT_EQ(status_$ok, st);
}

TEST(empty_slot_is_unknown_network)
{
    /* "tst.l (0x3c,A5,D1w*0x1) / beq" at 0x00E0F2F4 -> 0x00E0F302 */
    uint32_t out = 0xDEADBEEFu;
    status_$t st = 0;

    reset();
    NETWORK_$GET_NET(addr_with_index(5, 0), &out, &st);

    ASSERT_EQ(0, out);
    /* "move.l #0x110017,(A1)" at 0x00E0F304 */
    ASSERT_EQ(0x00110017u, st);
    ASSERT_EQ(status_$network_unknown_network, st);
}

TEST(every_slot_is_reachable)
{
    /* The index is six bits, so slots 1..0x3F all have to be selectable and
     * each must land on its own entry ("lsl.w #0x3" at 0x00E0F2F2). */
    uint16_t i;

    reset();
    for (i = 1; i <= 0x3F; i++) {
        NETWORK_$NET_TABLE[i].net_id = 0xC0000000u + i;
    }
    for (i = 1; i <= 0x3F; i++) {
        uint32_t out = 0;
        status_$t st = 0x7FFFFFFF;
        NETWORK_$GET_NET(addr_with_index(i, 0x000ABCDEu), &out, &st);
        ASSERT_EQ(0xC0000000u + i, out);
        ASSERT_EQ(status_$ok, st);
    }
}

TEST(get_index_macro)
{
    /* The mask applies to the HIGH word, so the index occupies bits 20..25
     * of the longword. */
    ASSERT_EQ(0x00, NETWORK_GET_INDEX(0x000003F0u));
    ASSERT_EQ(0x3F, NETWORK_GET_INDEX(0x03F00000u));
    ASSERT_EQ(0x01, NETWORK_GET_INDEX(0x00100000u));
    ASSERT_EQ(0x03, NETWORK_GET_INDEX(0x003F0000u));
    /* bits outside 0x3F0 of the high word are masked away */
    ASSERT_EQ(0x3F, NETWORK_GET_INDEX(0xFFFFFFFFu));
}

/* ============================================================================ */

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("NETWORK_$GET_NET tests\n");

    RUN_TEST(index_comes_from_the_high_word);
    RUN_TEST(low_word_bits_are_ignored);
    RUN_TEST(index_zero_yields_zero_and_ok);
    RUN_TEST(empty_slot_is_unknown_network);
    RUN_TEST(every_slot_is_reachable);
    RUN_TEST(get_index_macro);

    printf("\n%d tests, %d failed\n", tests_run, tests_failed);
    return tests_failed ? 1 : 0;
}
