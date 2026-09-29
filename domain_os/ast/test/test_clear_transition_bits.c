/*
 * ast/test/test_clear_transition_bits.c - Unit tests for
 *                                         ast_$clear_transition_bits (0x00E0283C)
 *
 * The test #includes ast/clear_transition_bits.c directly and drives the
 * real routine through mocked MMAP_$AVAIL and EC_$ADVANCE.  It pins:
 *
 *   - a zero count only advances the eventcount;
 *   - exactly `count` entries are touched (dbf on count - 1);
 *   - MMAP_$AVAIL is called only for installed entries (bit 30) whose page
 *     number is in 0x200..0xFFF, with the zero-extended low word;
 *   - bit 31 is cleared on every entry, installed or not.
 */

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#define uid_t ast_uid_t

static int tests_passed = 0;
static int tests_failed = 0;
static int current_failed = 0;

static void reset_state(void);

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do {                                                   \
    printf("  Running %-48s", #name);                                         \
    current_failed = 0;                                                       \
    reset_state();                                                            \
    test_##name();                                                            \
    if (current_failed == 0) { tests_passed++; printf("PASSED\n"); }          \
} while (0)

#define ASSERT_EQ(expected, actual) do {                                      \
    if ((unsigned long long)(expected) != (unsigned long long)(actual)) {      \
        printf("FAILED\n    Expected: 0x%llx, Got: 0x%llx at line %d\n",       \
               (unsigned long long)(expected),                                \
               (unsigned long long)(actual), __LINE__);                       \
        tests_failed++; current_failed = 1;                                   \
        return;                                                               \
    }                                                                         \
} while (0)

#include "ast/clear_transition_bits.c"

/* The AST_ module blocks (ast/ast.h). */
MODULE_DATA_DEFINE(ast_$data_t, AST_$DATA, 0x00E1DC80);
MODULE_DATA_DEFINE(ast_$aot_t, AST_$AOT, 0x00EC5400);

static int avail_calls;
static uint32_t avail_vpns[8];
void MMAP_$AVAIL(uint32_t vpn)
{
    if (avail_calls < 8) { avail_vpns[avail_calls] = vpn; }
    avail_calls++;
}

static int advance_calls;
static ec_$eventcount_t *advance_ec;
void EC_$ADVANCE(ec_$eventcount_t *ec)
{
    advance_calls++;
    advance_ec = ec;
}

static void reset_state(void)
{
    avail_calls = 0;
    memset(avail_vpns, 0, sizeof(avail_vpns));
    advance_calls = 0;
    advance_ec = NULL;
}

TEST(zero_count_only_advances)
{
    uint32_t map[2] = { 0xC0000345u, 0xC0000346u };

    ast_$clear_transition_bits(map, 0);

    ASSERT_EQ(0, avail_calls);
    ASSERT_EQ(1, advance_calls);
    ASSERT_EQ((uintptr_t)&AST_$PMAP_IN_TRANS_EC, (uintptr_t)advance_ec);
    ASSERT_EQ(0xC0000345u, map[0]);          /* untouched */
}

TEST(installed_in_range_made_available)
{
    uint32_t map[3] = { 0xC0000345u, 0x80123456u, 0xC0000FFFu };

    ast_$clear_transition_bits(map, 3);

    ASSERT_EQ(2, avail_calls);
    ASSERT_EQ(0x345, avail_vpns[0]);
    ASSERT_EQ(0xFFF, avail_vpns[1]);
    ASSERT_EQ(0x40000345u, map[0]);
    ASSERT_EQ(0x00123456u, map[1]);          /* not installed: bit 31 only */
    ASSERT_EQ(0x40000FFFu, map[2]);
    ASSERT_EQ(1, advance_calls);
}

TEST(out_of_range_ppn_skipped)
{
    uint32_t map[3] = { 0xC00001FFu, 0xC0001000u, 0xC0000200u };

    ast_$clear_transition_bits(map, 3);

    ASSERT_EQ(1, avail_calls);
    ASSERT_EQ(0x200, avail_vpns[0]);
    ASSERT_EQ(0x400001FFu, map[0]);
    ASSERT_EQ(0x40001000u, map[1]);
}

TEST(exactly_count_entries_touched)
{
    uint32_t map[4] = { 0xC0000300u, 0xC0000301u, 0xC0000302u, 0xC0000303u };

    ast_$clear_transition_bits(map, 2);

    ASSERT_EQ(2, avail_calls);
    ASSERT_EQ(0x40000300u, map[0]);
    ASSERT_EQ(0x40000301u, map[1]);
    ASSERT_EQ(0xC0000302u, map[2]);          /* beyond the count */
    ASSERT_EQ(0xC0000303u, map[3]);
}

int main(void)
{
    printf("test_clear_transition_bits (ast_$clear_transition_bits 0x00E0283C)\n");

    RUN_TEST(zero_count_only_advances);
    RUN_TEST(installed_in_range_made_available);
    RUN_TEST(out_of_range_ppn_skipped);
    RUN_TEST(exactly_count_entries_touched);

    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
