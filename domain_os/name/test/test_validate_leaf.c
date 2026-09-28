/*
 * name/test/test_validate_leaf.c - unit tests for name_$validate_leaf
 * (0x00E54414) with the real MAP_CASE (0x00E53EF8) underneath.
 *
 * The two OLD_DIR character sets and NAME_$LOCK_SLOT are defined here with
 * the image bytes (0xE7FD24 / 0xE7FD44 / 0xE7FD60).  The tests pin:
 *   - the 32-byte length gate                          0x00E5442A
 *   - the mapped-length gates and the '\' test        0x00E54452-0x00E54460
 *   - the first-character set (a bare '.' component)  0x00E54462-0x00E54476
 *   - the every-character set (no '/')                 0x00E54484-0x00E54498
 *   - a first byte below 0x20 reading its set bit out  0x00E5446E: A5+0x20
 *     of NAME_$LOCK_SLOT[0], because that set is only  is 28 bytes long
 *     28 bytes long
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>

static int tests_passed = 0;
static int tests_failed = 0;
static int current_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do {                     \
    printf("  %-52s ", #name);                  \
    current_failed = 0;                         \
    test_##name();                              \
    if (current_failed) { tests_failed++; }     \
    else { tests_passed++; printf("PASSED\n"); }\
} while (0)

#define ASSERT_EQ(expected, actual) do {                                 \
    long long _e = (long long)(expected);                                \
    long long _a = (long long)(actual);                                  \
    if (_e != _a) {                                                      \
        printf("FAILED\n    Expected %lld, got %lld at line %d\n",       \
               _e, _a, __LINE__);                                        \
        current_failed = 1;                                              \
        return;                                                          \
    }                                                                    \
} while (0)

#include "name/name_internal.h"

/* 0xE7FD24, 32 bytes: every byte of a leaf. */
uint8_t NAME_$LEAF_CHAR_SET[NAME_$LEAF_SET_SIZE] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x7f, 0xff, 0xff, 0xff, 0xef, 0xff, 0xff, 0xff,
    0xff, 0xff, 0x7f, 0xfe, 0x00, 0x00, 0x00, 0x00
};

/* 0xE7FD44, 28 bytes: the first byte of a leaf. */
uint8_t NAME_$LEAF_FIRST_CHAR_SET[NAME_$LEAF_FIRST_SET_SIZE] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x3f, 0xff, 0xff, 0xfe, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0x3f, 0xfe
};

/* 0xE7FD60: the lock slots that follow the second set. */
uint32_t NAME_$LOCK_SLOT[NAME_$MAX_LOCK_PROCS];

#include "../validate_leaf.c"
#include "../../file/map_case.c"

static uint8_t  parsed[64];
static uint16_t parsed_len;

static int8_t run(const char *name, uint16_t len)
{
    memset(parsed, 0x55, sizeof(parsed));
    parsed_len = 0x7777;
    return name_$validate_leaf((char *)name, len, parsed, &parsed_len);
}

/* 0x00E54480-0x00E5449C plus the 0x00E544A0 `st`: an ordinary name. */
TEST(plain_name_is_accepted)
{
    ASSERT_EQ(-1, run("hello", 5));
    ASSERT_EQ(5, parsed_len);
    ASSERT_EQ(0, memcmp(parsed, "HELLO", 5));   /* MAP_CASE upper-cases */
}

/* 0x00E54478-0x00E5447C: a one-byte name skips the loop and is accepted. */
TEST(single_byte_name_is_accepted)
{
    ASSERT_EQ(-1, run("x", 1));
    ASSERT_EQ(1, parsed_len);
}

/* 0x00E5442A `cmpi.w #0x20 / bhi`: 33 bytes rejected before MAP_CASE. */
TEST(name_longer_than_32_is_rejected)
{
    ASSERT_EQ(0, run("abcdefghijklmnopqrstuvwxyzabcdefg", 33));
    ASSERT_EQ(0x7777, parsed_len);              /* MAP_CASE never ran */
    ASSERT_EQ(-1, run("abcdefghijklmnopqrstuvwxyzabcdef", 32));
}

/* 0x00E54452: a zero mapped length is rejected. */
TEST(empty_name_is_rejected)
{
    ASSERT_EQ(0, run("", 0));
}

/* 0x00E54456: 17 upper-case bytes map to 34 (":A" each) - MAP_CASE reports
 * truncation into the 32-byte limit, 0x00E5444C `bmi` rejects. */
TEST(mapped_length_over_32_is_rejected)
{
    ASSERT_EQ(0, run("ABCDEFGHIJKLMNOPQ", 17));
}

/* 0x00E5445C `cmpi.b #0x5c,(A2)`: the test is on the MAPPED first byte, and
 * MAP_CASE turns '\' into ":|", so a leading backslash arrives as ':' -
 * which is in the first-byte set - and is accepted.  Reproduced as found. */
TEST(leading_backslash_maps_to_colon_and_is_accepted)
{
    ASSERT_EQ(-1, run("\\abc", 4));
    ASSERT_EQ(':', parsed[0]);
    ASSERT_EQ('|', parsed[1]);
}

/* 0x00E54462-0x00E54476: '.' is not in the first-byte set.  MAP_CASE only
 * lets a '.' through unescaped as a whole "." or ".." component; ".hidden",
 * "`node" and "~user" become ":.HIDDEN", ":`NODE" and ":~USER" and pass. */
TEST(dot_component_is_rejected_but_escaped_leads_pass)
{
    ASSERT_EQ(0, run(".", 1));
    ASSERT_EQ(0, run("..", 2));
    ASSERT_EQ(-1, run(".hidden", 7));
    ASSERT_EQ(':', parsed[0]);
    ASSERT_EQ('.', parsed[1]);
    ASSERT_EQ(-1, run("`node", 5));
    ASSERT_EQ(-1, run("~user", 5));
    ASSERT_EQ(-1, run("a.b", 3));
}

/* 0x00E54484-0x00E54498: '/' is in neither set. */
TEST(slash_inside_name_is_rejected)
{
    ASSERT_EQ(0, run("a/b", 3));
}

/*
 * 0x00E5446E `lea (0x20,A5),A0` + 0x00E54472 `btst.b D4,(0x0,A0,D5w*1)`
 * with ch = 0: byte (0xFF-0)>>3 = 31 of a 28-byte set, i.e. byte 3 of
 * NAME_$LOCK_SLOT[0], bit 0.  A NUL passes through MAP_CASE unchanged.
 */
TEST(nul_first_byte_reads_lock_slot_zero)
{
    NAME_$LOCK_SLOT[0] = 0;
    ASSERT_EQ(0, run("\0", 1));

    NAME_$LOCK_SLOT[0] = 0x00000001u;           /* byte 3 bit 0 */
    ASSERT_EQ(-1, run("\0", 1));

    NAME_$LOCK_SLOT[0] = 0x00000100u;           /* byte 2 bit 0: not consulted */
    ASSERT_EQ(0, run("\0", 1));

    /* ch = 0x1F: byte (0xFF-0x1F)>>3 = 28 = NAME_$LOCK_SLOT[0] byte 0, bit 7.
     * MAP_CASE hex-escapes 0x1F, so drive the set lookup directly. */
    NAME_$LOCK_SLOT[0] = 0x80000000u;
    ASSERT_EQ(0x80, name_$leaf_first_set_byte(28));
    NAME_$LOCK_SLOT[0] = 0;
}

int main(void)
{
    printf("name_$validate_leaf tests\n");
    RUN_TEST(plain_name_is_accepted);
    RUN_TEST(single_byte_name_is_accepted);
    RUN_TEST(name_longer_than_32_is_rejected);
    RUN_TEST(empty_name_is_rejected);
    RUN_TEST(mapped_length_over_32_is_rejected);
    RUN_TEST(leading_backslash_maps_to_colon_and_is_accepted);
    RUN_TEST(dot_component_is_rejected_but_escaped_leads_pass);
    RUN_TEST(slash_inside_name_is_rejected);
    RUN_TEST(nul_first_byte_reads_lock_slot_zero);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
