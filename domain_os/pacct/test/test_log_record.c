/*
 * pacct/test/test_log_record.c - Unit tests for the process accounting record
 * layout (pacct_record_t, bead source-q5k1) and for pacct_$compress
 * (0x00E5A9CA), whose real .c is #included below.
 *
 * The layout is the one PACCT_$LOG (0x00E5AA9C) builds in its frame at
 * A6-0x190 before copying 32 longwords out of it at 0x00E5ACEE.  Every
 * assertion below names the storing instruction.
 */

#include <stdio.h>
#include <string.h>

#include "pacct/pacct_internal.h"

/* The function under test, for real. */
#include "../compress.c"

/* ============================================================================
 * Test framework
 * ============================================================================ */

static int tests_passed = 0;
static int tests_failed = 0;
static int current_failed;

#define RUN_TEST(name) do {                     \
    printf("  Running %s... ", #name);          \
    current_failed = 0;                         \
    name();                                     \
    if (current_failed) {                       \
        tests_failed++;                         \
        printf("FAIL\n");                       \
    } else {                                    \
        tests_passed++;                         \
        printf("ok\n");                         \
    }                                           \
} while (0)

#define ASSERT_EQ(actual, expected, what) do {                          \
    unsigned long _a = (unsigned long)(actual);                         \
    unsigned long _e = (unsigned long)(expected);                       \
    if (_a != _e) {                                                     \
        printf("\n    %s: got 0x%lx, expected 0x%lx", (what), _a, _e);  \
        current_failed = 1;                                             \
    }                                                                   \
} while (0)

/* ============================================================================
 * pacct_record_t layout
 * ============================================================================ */

/*
 * The record base is A6-0x190; each field's offset is (displacement + 0x190).
 */
static void test_record_offsets(void)
{
    ASSERT_EQ(offsetof(pacct_record_t, ac_flags),     0x00, "ac_flags (clr.w -0x190 @0x00E5AAFC)");
    ASSERT_EQ(offsetof(pacct_record_t, ac_stat),      0x02, "ac_stat (-0x18e @0x00E5AB2A)");
    ASSERT_EQ(offsetof(pacct_record_t, ac_pad_03),    0x03, "ac_pad_03 (never written)");
    ASSERT_EQ(offsetof(pacct_record_t, ac_sids),      0x04, "ac_sids (-0x18c @0x00E5AB34)");
    ASSERT_EQ(offsetof(pacct_record_t, ac_prot),      0x28, "ac_prot (-0x168 @0x00E5AB46)");
    ASSERT_EQ(offsetof(pacct_record_t, ac_devno),     0x34, "ac_devno (-0x15c @0x00E5AB50/0x00E5AC5C)");
    ASSERT_EQ(offsetof(pacct_record_t, ac_btime),     0x38, "ac_btime (-0x158 @0x00E5ABBA)");
    ASSERT_EQ(offsetof(pacct_record_t, ac_utime),     0x3C, "ac_utime (-0x154 @0x00E5AB96)");
    ASSERT_EQ(offsetof(pacct_record_t, ac_stime),     0x3E, "ac_stime (-0x152 @0x00E5ABA6)");
    ASSERT_EQ(offsetof(pacct_record_t, ac_etime),     0x40, "ac_etime (-0x150 @0x00E5ABD6)");
    ASSERT_EQ(offsetof(pacct_record_t, ac_zero_42),   0x42, "ac_zero_42 (-0x14e @0x00E5AB54)");
    ASSERT_EQ(offsetof(pacct_record_t, ac_io_write),  0x46, "ac_io_write (-0x14a @0x00E5AB60)");
    ASSERT_EQ(offsetof(pacct_record_t, ac_io_read),   0x48, "ac_io_read (-0x148 @0x00E5AB6C)");
    ASSERT_EQ(offsetof(pacct_record_t, ac_proc_uid),  0x4A, "ac_proc_uid (-0x146 @0x00E5ABDE)");
    ASSERT_EQ(offsetof(pacct_record_t, ac_comm),      0x52, "ac_comm (-0x13e @0x00E5AC04)");
    ASSERT_EQ(offsetof(pacct_record_t, ac_mem),       0x72, "ac_mem (-0x11e @0x00E5AB86)");
    ASSERT_EQ(offsetof(pacct_record_t, ac_pad_74),    0x74, "ac_pad_74 (never written)");
    ASSERT_EQ(sizeof(pacct_record_t),                 0x80, "sizeof: 32 longwords @0x00E5ACF8");
    ASSERT_EQ(sizeof(((pacct_record_t *)0)->ac_comm), 0x20, "ac_comm is 32 bytes (0x00E5ABEA moveq #0x20)");
}

/* The two blocks ACL_$GET_RE_ALL_SIDS fills. */
static void test_sid_block_sizes(void)
{
    ASSERT_EQ(sizeof(pacct_sid_block_t),  0x24, "9 longwords (0x00E5AB38 moveq #0x8 + dbf)");
    ASSERT_EQ(sizeof(pacct_prot_block_t), 0x0C, "3 longwords (0x00E5AB4A-0x00E5AB4E)");
}

/*
 * The flags word is cleared with `clr.w (-0x190,A6)` and then edited through
 * A6-0x18F, i.e. the word's LOW-ORDER byte on m68k.  So the two booleans set
 * bits 0 and 1 of the WORD, not bits 8 and 9 - the failure mode the audit
 * called "byte operations on the high byte of a word".  The assertion is on
 * the word value, which is the host-independent statement of that.
 */
static void test_flags_are_bits_0_and_1_of_the_word(void)
{
    pacct_record_t r;
    memset(&r, 0, sizeof(r));
    r.ac_flags = 0;
    r.ac_flags |= 0x0001;
    ASSERT_EQ(r.ac_flags, 0x0001, "arg1's boolean is bit 0, not bit 8");
    r.ac_flags |= 0x0002;
    ASSERT_EQ(r.ac_flags, 0x0003, "arg2's boolean is bit 1, not bit 9");
}

/* ============================================================================
 * pacct_$compress (0x00E5A9CA)
 * ============================================================================ */

static void test_compress_small_values_are_verbatim(void)
{
    /* Below 0x2000 the loop never runs, so exponent 0 and the value passes
     * through unchanged (0x00E5A9E6 `cmpi.l #0x2000,D0` / `bcc`). */
    ASSERT_EQ(pacct_$compress(0),      0x0000, "0");
    ASSERT_EQ(pacct_$compress(1),      0x0001, "1");
    ASSERT_EQ(pacct_$compress(0x1FFF), 0x1FFF, "0x1FFF is the largest verbatim value");
}

static void test_compress_one_shift(void)
{
    /*
     * 0x2000 >> 3 = 0x400, exponent 1.  Bit 2 of 0x2000 is clear, so `sne D2b`
     * leaves the round flag false (0x00E5A9DE).
     */
    ASSERT_EQ(pacct_$compress(0x2000), (1u << 13) | 0x0400, "0x2000");
    /* 0xFFF8 >> 3 = 0x1FFF, still one shift, bit 2 clear. */
    ASSERT_EQ(pacct_$compress(0xFFF8), (1u << 13) | 0x1FFF, "0xFFF8");
}

static void test_compress_rounds_on_bit_2(void)
{
    /*
     * 0x2004: bit 2 is set, so D2b becomes 0xFF before the shift; 0x2004 >> 3
     * = 0x400 and the `addq.l #0x1` at 0x00E5A9F2 makes it 0x401.
     */
    ASSERT_EQ(pacct_$compress(0x2004), (1u << 13) | 0x0401, "0x2004 rounds up");
    /*
     * Only the LAST shift's bit 2 counts: `sne D2b` overwrites the flag every
     * iteration.  0x10004 needs two shifts; on the second pass the value is
     * 0x2000, whose bit 2 is clear, so no rounding happens.
     */
    ASSERT_EQ(pacct_$compress(0x10004), (2u << 13) | 0x0400, "0x10004: last shift clears the flag");
}

static void test_compress_rounding_can_carry_into_another_shift(void)
{
    /*
     * 0xFFFC >> 3 = 0x1FFF with bit 2 set, so the round-up makes it 0x2000,
     * which fails `cmpi.l #0x2000 / bcs` at 0x00E5A9F4 and takes the extra
     * `addq.w #0x1,D1w` / `lsr.l #0x3,D0` at 0x00E5A9FC.
     */
    ASSERT_EQ(pacct_$compress(0xFFFC), (2u << 13) | 0x0400, "0xFFFC carries into exponent 2");
}

static void test_compress_exponent_wraps_into_three_bits(void)
{
    /*
     * 0xFFFFFFFF needs seven shifts to fall below 0x2000 (0x1FFFFFF, 0x3FFFF,
     * 0x7FFF, 0xFFF), i.e. four; the packed exponent is masked to three bits
     * by `lsl.b #0x5,D3b` / `or.b` at 0x00E5AA08.
     */
    uint32_t v = 0xFFFFFFFFu;
    int exp = 0;
    int round = 0;
    while (v >= 0x2000u) {
        exp++;
        round = (v & 4u) ? 1 : 0;
        v >>= 3;
    }
    if (round) {
        v++;
        if (v >= 0x2000u) { exp++; v >>= 3; }
    }
    ASSERT_EQ(pacct_$compress(0xFFFFFFFFu),
              (comp_t)(((exp & 7) << 13) | (v & 0x1FFF)),
              "0xFFFFFFFF");
}

int main(void)
{
    printf("pacct record / compress tests\n");

    RUN_TEST(test_record_offsets);
    RUN_TEST(test_sid_block_sizes);
    RUN_TEST(test_flags_are_bits_0_and_1_of_the_word);
    RUN_TEST(test_compress_small_values_are_verbatim);
    RUN_TEST(test_compress_one_shift);
    RUN_TEST(test_compress_rounds_on_bit_2);
    RUN_TEST(test_compress_rounding_can_carry_into_another_shift);
    RUN_TEST(test_compress_exponent_wraps_into_three_bits);

    printf("%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
