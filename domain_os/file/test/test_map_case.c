/*
 * file/test/test_map_case.c - Unit tests for MAP_CASE
 *
 * Tests the Unix -> Domain/OS case mapping function.
 * MAP_CASE is a pure function with no kernel dependencies,
 * so we can test it directly without mocks.
 */

#include <stdio.h>
#include <assert.h>
#include <stdint.h>
#include <string.h>

/* Minimal type stubs for building outside the kernel */
typedef long status_$t;
#define status_$ok 0

/* Test result tracking */
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    printf("  Running %s... ", #name); \
    test_##name(); \
    tests_passed++; \
    printf("PASSED\n"); \
} while(0)

#define ASSERT_EQ(expected, actual) do { \
    if ((expected) != (actual)) { \
        printf("FAILED\n    Expected: 0x%lx, Got: 0x%lx at line %d\n", \
               (unsigned long)(expected), (unsigned long)(actual), __LINE__); \
        tests_failed++; \
        return; \
    } \
} while(0)

#define ASSERT_MEM_EQ(expected, actual, len) do { \
    if (memcmp((expected), (actual), (len)) != 0) { \
        printf("FAILED\n    Memory mismatch at line %d\n", __LINE__); \
        printf("    Expected: "); \
        for (int _i = 0; _i < (int)(len); _i++) printf("%02x ", ((unsigned char*)(expected))[_i]); \
        printf("\n    Got:      "); \
        for (int _i = 0; _i < (int)(len); _i++) printf("%02x ", ((unsigned char*)(actual))[_i]); \
        printf("\n"); \
        tests_failed++; \
        return; \
    } \
} while(0)

/* Include the implementation directly for testing */
/* We need to provide the header stubs first */
#ifndef FILE_H
#define FILE_H
#include <stdint.h>
#endif

/* Pull in the implementation */
#include "../map_case.c"

/* Helper to run MAP_CASE with string inputs */
static void run_map_case(const char *input, char *output, int16_t max_out,
                         int16_t *out_len, uint8_t *truncated)
{
    int16_t in_len = (int16_t)strlen(input);
    int16_t max_out_len = max_out;
    MAP_CASE((char *)input, &in_len, output, &max_out_len, out_len, truncated);
}

/*
 * Test: lowercase letters are converted to uppercase
 */
TEST(lowercase_to_uppercase)
{
    char output[64];
    int16_t out_len;
    uint8_t truncated;

    memset(output, 0, sizeof(output));
    run_map_case("hello", output, 64, &out_len, &truncated);

    ASSERT_EQ(0, truncated);
    ASSERT_EQ(5, out_len);
    ASSERT_MEM_EQ("HELLO", output, 5);
}

/*
 * Test: uppercase letters are escaped with ':' prefix
 */
TEST(uppercase_escaped)
{
    char output[64];
    int16_t out_len;
    uint8_t truncated;

    memset(output, 0, sizeof(output));
    run_map_case("Hello", output, 64, &out_len, &truncated);

    ASSERT_EQ(0, truncated);
    ASSERT_EQ(6, out_len);
    /* 'H' -> ':H', 'ello' -> 'ELLO' */
    ASSERT_MEM_EQ(":HELLO", output, 6);
}

/*
 * Test: all uppercase input
 */
TEST(all_uppercase)
{
    char output[64];
    int16_t out_len;
    uint8_t truncated;

    memset(output, 0, sizeof(output));
    run_map_case("ABC", output, 64, &out_len, &truncated);

    ASSERT_EQ(0, truncated);
    ASSERT_EQ(6, out_len);
    ASSERT_MEM_EQ(":A:B:C", output, 6);
}

/*
 * Test: single dot at end or before slash passes through
 */
TEST(dot_passthrough)
{
    char output[64];
    int16_t out_len;
    uint8_t truncated;

    /* Single '.' at end */
    memset(output, 0, sizeof(output));
    run_map_case(".", output, 64, &out_len, &truncated);

    ASSERT_EQ(0, truncated);
    ASSERT_EQ(1, out_len);
    ASSERT_EQ('.', output[0]);

    /* Single '.' before '/' */
    memset(output, 0, sizeof(output));
    run_map_case("./foo", output, 64, &out_len, &truncated);

    ASSERT_EQ(0, truncated);
    ASSERT_EQ(5, out_len);
    ASSERT_MEM_EQ("./FOO", output, 5);
}

/*
 * Test: double dot passes through
 */
TEST(dotdot_passthrough)
{
    char output[64];
    int16_t out_len;
    uint8_t truncated;

    /* '..' at end */
    memset(output, 0, sizeof(output));
    run_map_case("..", output, 64, &out_len, &truncated);

    ASSERT_EQ(0, truncated);
    ASSERT_EQ(2, out_len);
    ASSERT_MEM_EQ("..", output, 2);

    /* '../' */
    memset(output, 0, sizeof(output));
    run_map_case("../foo", output, 64, &out_len, &truncated);

    ASSERT_EQ(0, truncated);
    ASSERT_EQ(6, out_len);
    ASSERT_MEM_EQ("../FOO", output, 6);
}

/*
 * Test: dot followed by other chars at start of component is escaped
 */
TEST(dot_hidden_file)
{
    char output[64];
    int16_t out_len;
    uint8_t truncated;

    /* '.hidden' -> ':.HIDDEN' */
    memset(output, 0, sizeof(output));
    run_map_case(".hidden", output, 64, &out_len, &truncated);

    ASSERT_EQ(0, truncated);
    ASSERT_EQ(8, out_len);
    ASSERT_MEM_EQ(":.HIDDEN", output, 8);
}

/*
 * Test: backtick at start of component is escaped
 */
TEST(backtick_escaped)
{
    char output[64];
    int16_t out_len;
    uint8_t truncated;

    memset(output, 0, sizeof(output));
    run_map_case("`foo", output, 64, &out_len, &truncated);

    ASSERT_EQ(0, truncated);
    ASSERT_EQ(5, out_len);
    ASSERT_MEM_EQ(":`FOO", output, 5);
}

/*
 * Test: tilde at start of component is escaped
 */
TEST(tilde_escaped)
{
    char output[64];
    int16_t out_len;
    uint8_t truncated;

    memset(output, 0, sizeof(output));
    run_map_case("~user", output, 64, &out_len, &truncated);

    ASSERT_EQ(0, truncated);
    ASSERT_EQ(6, out_len);
    ASSERT_MEM_EQ(":~USER", output, 6);
}

/*
 * Test: space is escaped as ':_'
 */
TEST(space_escaped)
{
    char output[64];
    int16_t out_len;
    uint8_t truncated;

    memset(output, 0, sizeof(output));
    run_map_case("a b", output, 64, &out_len, &truncated);

    ASSERT_EQ(0, truncated);
    ASSERT_EQ(4, out_len);
    ASSERT_MEM_EQ("A:_B", output, 4);
}

/*
 * Test: backslash is escaped as ':|'
 */
TEST(backslash_escaped)
{
    char output[64];
    int16_t out_len;
    uint8_t truncated;

    memset(output, 0, sizeof(output));
    run_map_case("a\\b", output, 64, &out_len, &truncated);

    ASSERT_EQ(0, truncated);
    ASSERT_EQ(4, out_len);
    ASSERT_MEM_EQ("A:|B", output, 4);
}

/*
 * Test: colon is escaped as '::'
 */
TEST(colon_escaped)
{
    char output[64];
    int16_t out_len;
    uint8_t truncated;

    memset(output, 0, sizeof(output));
    run_map_case("a:b", output, 64, &out_len, &truncated);

    ASSERT_EQ(0, truncated);
    ASSERT_EQ(4, out_len);
    ASSERT_MEM_EQ("A::B", output, 4);
}

/*
 * Test: control characters are hex-escaped as ':#XX'
 */
TEST(control_char_hex_escaped)
{
    char output[64];
    int16_t out_len;
    uint8_t truncated;

    /* Tab (0x09) -> ':#09' */
    char input[] = { 'a', 0x09, 'b', 0 };
    memset(output, 0, sizeof(output));
    {
        int16_t in_len = 3;
        int16_t max_out = 64;
        MAP_CASE(input, &in_len, output, &max_out, &out_len, &truncated);
    }

    ASSERT_EQ(0, truncated);
    ASSERT_EQ(6, out_len);
    ASSERT_MEM_EQ("A:#09B", output, 6);
}

/*
 * Test: high-bit characters (>= 0x7F) are hex-escaped
 *
 * Note: The original assembly only applies the conditional hex encoding
 * (0x30 for 0-9, 0x57 for a-f) to the LOW nibble. The HIGH nibble
 * unconditionally gets + 0x30, which means high nibbles >= 10 produce
 * non-standard characters (':' through '?'). This matches the original.
 */
TEST(high_bit_hex_escaped)
{
    char output[64];
    int16_t out_len;
    uint8_t truncated;

    /* 0x7F -> ':#7f' (high nibble 7 -> '7', low nibble F -> 'f') */
    char input[] = { (char)0x7F, 0 };
    memset(output, 0, sizeof(output));
    {
        int16_t in_len = 1;
        int16_t max_out = 64;
        MAP_CASE(input, &in_len, output, &max_out, &out_len, &truncated);
    }

    ASSERT_EQ(0, truncated);
    ASSERT_EQ(4, out_len);
    ASSERT_MEM_EQ(":#7f", output, 4);

    /*
     * 0xFF -> ':#?f'
     * High nibble: 0xF + 0x30 = 0x3F = '?'
     * Low nibble: 0xF -> 'f' (via 0x57 path)
     * This is the original behavior - high nibble encoding is not proper hex.
     */
    input[0] = (char)0xFF;
    memset(output, 0, sizeof(output));
    {
        int16_t in_len = 1;
        int16_t max_out = 64;
        MAP_CASE(input, &in_len, output, &max_out, &out_len, &truncated);
    }

    ASSERT_EQ(0, truncated);
    ASSERT_EQ(4, out_len);
    ASSERT_MEM_EQ(":#?f", output, 4);
}

/*
 * Test: hex encoding behavior
 *
 * Low nibble uses lowercase hex (a-f) for values >= 10.
 * High nibble uses unconditional + 0x30 (only correct for 0-9).
 */
TEST(hex_lowercase)
{
    char output[64];
    int16_t out_len;
    uint8_t truncated;

    /*
     * 0xAB:
     * High nibble: 0xA + 0x30 = 0x3A = ':'
     * Low nibble: 0xB -> 'b' (via 0x57 path)
     * Result: ':#:b'
     */
    char input[] = { (char)0xAB, 0 };
    memset(output, 0, sizeof(output));
    {
        int16_t in_len = 1;
        int16_t max_out = 64;
        MAP_CASE(input, &in_len, output, &max_out, &out_len, &truncated);
    }

    ASSERT_EQ(0, truncated);
    ASSERT_EQ(4, out_len);
    ASSERT_MEM_EQ(":#:b", output, 4);

    /*
     * 0x19 (control char, high nibble 1, low nibble 9):
     * High nibble: 1 + 0x30 = 0x31 = '1'
     * Low nibble: 9 + 0x30 = 0x39 = '9'
     * Result: ':#19' (correct standard hex for values with nibbles 0-9)
     */
    input[0] = (char)0x19;
    memset(output, 0, sizeof(output));
    {
        int16_t in_len = 1;
        int16_t max_out = 64;
        MAP_CASE(input, &in_len, output, &max_out, &out_len, &truncated);
    }

    ASSERT_EQ(0, truncated);
    ASSERT_EQ(4, out_len);
    ASSERT_MEM_EQ(":#19", output, 4);
}

/*
 * Test: slash passes through and resets component tracking
 */
TEST(slash_passthrough)
{
    char output[64];
    int16_t out_len;
    uint8_t truncated;

    memset(output, 0, sizeof(output));
    run_map_case("foo/bar", output, 64, &out_len, &truncated);

    ASSERT_EQ(0, truncated);
    ASSERT_EQ(7, out_len);
    ASSERT_MEM_EQ("FOO/BAR", output, 7);
}

/*
 * Test: component start tracking across '/'
 * Dot/backtick/tilde should only be escaped at component start
 */
TEST(component_start_after_slash)
{
    char output[64];
    int16_t out_len;
    uint8_t truncated;

    /* '.hidden' after slash should still be escaped */
    memset(output, 0, sizeof(output));
    run_map_case("foo/.hidden", output, 64, &out_len, &truncated);

    ASSERT_EQ(0, truncated);
    ASSERT_EQ(12, out_len);
    ASSERT_MEM_EQ("FOO/:.HIDDEN", output, 12);
}

/*
 * Test: dot/backtick/tilde NOT at component start pass through normally
 */
TEST(special_not_at_component_start)
{
    char output[64];
    int16_t out_len;
    uint8_t truncated;

    /* '.' in middle of component is just a regular char */
    memset(output, 0, sizeof(output));
    run_map_case("foo.c", output, 64, &out_len, &truncated);

    ASSERT_EQ(0, truncated);
    ASSERT_EQ(5, out_len);
    ASSERT_MEM_EQ("FOO.C", output, 5);
}

/*
 * Test: output buffer overflow - truncation flag set
 */
TEST(truncation)
{
    char output[4];
    int16_t out_len;
    uint8_t truncated;

    memset(output, 0, sizeof(output));
    run_map_case("abcdefgh", output, 4, &out_len, &truncated);

    /* Should have truncated after 4 chars */
    ASSERT_EQ(0xFF, truncated);
    ASSERT_EQ(4, out_len);
    ASSERT_MEM_EQ("ABCD", output, 4);
}

/*
 * Test: truncation during multi-byte escape
 */
TEST(truncation_during_escape)
{
    char output[3];
    int16_t out_len;
    uint8_t truncated;

    /* 'aB' needs 3 bytes ('A' + ':B'), buffer is 3 -> fits */
    memset(output, 0, sizeof(output));
    run_map_case("aB", output, 3, &out_len, &truncated);
    ASSERT_EQ(0, truncated);
    ASSERT_EQ(3, out_len);
    ASSERT_MEM_EQ("A:B", output, 3);

    /* 'aBC' needs 5 bytes ('A' + ':B' + ':C'), buffer is 3 -> truncated at 3 */
    memset(output, 0, sizeof(output));
    run_map_case("aBC", output, 3, &out_len, &truncated);
    ASSERT_EQ(0xFF, truncated);
    ASSERT_EQ(3, out_len);
    ASSERT_MEM_EQ("A:B", output, 3);
}

/*
 * Test: empty input
 */
TEST(empty_input)
{
    char output[64];
    int16_t out_len;
    uint8_t truncated;

    memset(output, 0xAA, sizeof(output));
    run_map_case("", output, 64, &out_len, &truncated);

    ASSERT_EQ(0, truncated);
    ASSERT_EQ(0, out_len);
}

/*
 * Test: zero-length input (name_len = 0)
 */
TEST(zero_length)
{
    char output[64];
    int16_t out_len = 99;
    uint8_t truncated = 0xAA;

    int16_t in_len = 0;
    int16_t max_out = 64;
    MAP_CASE("anything", &in_len, output, &max_out, &out_len, &truncated);

    ASSERT_EQ(0, truncated);
    ASSERT_EQ(0, out_len);
}

/*
 * Test: digits and other printable chars pass through
 */
TEST(digits_passthrough)
{
    char output[64];
    int16_t out_len;
    uint8_t truncated;

    memset(output, 0, sizeof(output));
    run_map_case("123", output, 64, &out_len, &truncated);

    ASSERT_EQ(0, truncated);
    ASSERT_EQ(3, out_len);
    ASSERT_MEM_EQ("123", output, 3);
}

/*
 * Test: mixed case path
 */
TEST(mixed_path)
{
    char output[64];
    int16_t out_len;
    uint8_t truncated;

    memset(output, 0, sizeof(output));
    run_map_case("/usr/local/bin", output, 64, &out_len, &truncated);

    ASSERT_EQ(0, truncated);
    ASSERT_EQ(14, out_len);
    ASSERT_MEM_EQ("/USR/LOCAL/BIN", output, 14);
}

int main(void)
{
    printf("MAP_CASE tests:\n");

    RUN_TEST(lowercase_to_uppercase);
    RUN_TEST(uppercase_escaped);
    RUN_TEST(all_uppercase);
    RUN_TEST(dot_passthrough);
    RUN_TEST(dotdot_passthrough);
    RUN_TEST(dot_hidden_file);
    RUN_TEST(backtick_escaped);
    RUN_TEST(tilde_escaped);
    RUN_TEST(space_escaped);
    RUN_TEST(backslash_escaped);
    RUN_TEST(colon_escaped);
    RUN_TEST(control_char_hex_escaped);
    RUN_TEST(high_bit_hex_escaped);
    RUN_TEST(hex_lowercase);
    RUN_TEST(slash_passthrough);
    RUN_TEST(component_start_after_slash);
    RUN_TEST(special_not_at_component_start);
    RUN_TEST(truncation);
    RUN_TEST(truncation_during_escape);
    RUN_TEST(empty_input);
    RUN_TEST(zero_length);
    RUN_TEST(digits_passthrough);
    RUN_TEST(mixed_path);

    printf("\nResults: %d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
