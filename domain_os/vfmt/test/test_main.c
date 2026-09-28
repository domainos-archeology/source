/*
 * vfmt/test/test_main.c - unit tests for VFMT_$MAIN (0x00E6AB2A) and its
 * nested procedures, driven with the real M$ARITH divide routines.
 *
 * The argument list is an array of longword pointers, as the kernel's
 * callers build it on the stack.
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define RUN_TEST(name) do { \
    printf("  Running %s... ", #name); \
    test_##name(); \
    printf("PASSED\n"); \
    tests_passed++; \
} while (0)

#define ASSERT_EQ(expected, actual) do { \
    if ((unsigned long)(expected) != (unsigned long)(actual)) { \
        printf("FAILED\n    Expected: 0x%lx, Got: 0x%lx at line %d\n", \
               (unsigned long)(expected), (unsigned long)(actual), __LINE__); \
        tests_failed++; \
        return; \
    } \
} while (0)

#define ASSERT_STR(expected, buf, len) do { \
    if ((int)strlen(expected) != (int)(len) || memcmp(expected, buf, len) != 0) { \
        printf("FAILED\n    Expected: \"%s\", Got: \"%.*s\" at line %d\n", \
               expected, (int)(len), buf, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while (0)

#include "math/div.c"
#include "math/mod.c"
#include "vfmt/main.c"

static char out[256];
static int16_t max_len;
static int16_t out_len;

static void run(const char *fmt, void **args)
{
    memset(out, '#', sizeof(out));
    VFMT_$MAIN(fmt, out, &max_len, &out_len, args);
}

static void test_literal_and_end(void)
{
    max_len = 100;
    run("hello%$", NULL);
    ASSERT_STR("hello", out, out_len);
    run("a%%b%/c%.", NULL);
    ASSERT_STR("a%b\nc\n", out, out_len);
}

/* %d on a longword pointer; a leading-zero-stripped 20-digit field */
static void test_decimal(void)
{
    int32_t v = 12345;
    void *args[] = { &v };
    max_len = 100;
    run("%d%$", args);
    ASSERT_STR("12345", out, out_len);
    v = 0;
    run("[%d]%$", args);
    ASSERT_STR("[0]", out, out_len);
}

/* %wd reads a word; unsigned by default, S makes it signed */
static void test_word_signed(void)
{
    int16_t w = -5;
    void *args[] = { &w };
    max_len = 100;
    run("%wd%$", args);
    ASSERT_STR("65531", out, out_len);
    run("%wsd%$", args);
    ASSERT_STR("-5", out, out_len);
    run("%wpd%$", args);
    ASSERT_STR("-5", out, out_len);
    w = 7;
    run("%wpd%$", args);
    ASSERT_STR("+7", out, out_len);
}

/* %lh: hex; a width right-justifies with blanks, J left-justifies, Z keeps
 * the zeros */
static void test_hex_width(void)
{
    uint32_t v = 0xBEEF;
    void *args[] = { &v };
    max_len = 100;
    run("%lh%$", args);
    ASSERT_STR("BEEF", out, out_len);
    run("%8lh%$", args);
    ASSERT_STR("    BEEF", out, out_len);
    run("%8jlh%$", args);
    ASSERT_STR("BEEF    ", out, out_len);
    run("%6zlh%$", args);
    ASSERT_STR("00BEEF", out, out_len);
}

/* octal, and a bad modifier answers '?' */
static void test_octal_and_bad(void)
{
    uint32_t v = 8;
    void *args[] = { &v };
    max_len = 100;
    run("%lo%$", args);
    ASSERT_STR("10", out, out_len);
    run("%qd%$", args);
    ASSERT_STR("?", out, out_len);
}

/* %a takes (text, &length); U and L fold case; a width caps the length */
static void test_string(void)
{
    const char *s = "Hello World";
    int16_t n = 11;
    void *args[] = { (void *)s, &n };
    max_len = 100;
    run("<%a>%$", args);
    ASSERT_STR("<Hello World>", out, out_len);
    run("<%ua>%$", args);
    ASSERT_STR("<HELLO WORLD>", out, out_len);
    run("<%la>%$", args);
    ASSERT_STR("<hello world>", out, out_len);
    run("<%5a>%$", args);
    ASSERT_STR("<Hello>", out, out_len);
    run("<%15a>%$", args);
    ASSERT_STR("<Hello World    >", out, out_len);
}

/* without Z and without a width, trailing blanks (0x20 or 0xA0) are
 * dropped - blanks before a character are kept in full; a length word
 * <= 0 takes the word after it */
static void test_string_compress_and_long_length(void)
{
    const char *s = "  a   b  ";
    int16_t n = 9;
    int32_t n32 = 9;
    void *args[] = { (void *)s, &n };
    void *args32[] = { (void *)s, &n32 };
    max_len = 100;
    run("<%a>%$", args);
    ASSERT_STR("<  a   b>", out, out_len);
    run("<%za>%$", args);
    ASSERT_STR("<  a   b  >", out, out_len);
    run("<%za>%$", args32);
    ASSERT_STR("<  a   b  >", out, out_len);
}

/* %Mna takes only the text pointer, with n as the length */
static void test_string_m_modifier(void)
{
    const char *s = "abcdef";
    int32_t after = 99;
    void *args[] = { (void *)s, &after };
    max_len = 100;
    run("%m3a%d%$", args);
    ASSERT_STR("abc99", out, out_len);
}

/* %t tabs to a column (blank-filling), %x emits blanks, %( %) repeats */
static void test_tab_x_repeat(void)
{
    int16_t col = 6;
    void *args[] = { &col };
    max_len = 100;
    run("ab%5tc%$", NULL);
    ASSERT_STR("ab  c", out, out_len);
    run("ab%tc%$", args);
    ASSERT_STR("ab   c", out, out_len);
    run("a%3xb%$", NULL);
    ASSERT_STR("a   b", out, out_len);
    run("%3(ab%)c%$", NULL);
    ASSERT_STR("abababc", out, out_len);
}

/* output stops at max_len; a modifier string over ten characters or a
 * 200-character format without an end marker gives "??" */
static void test_limits(void)
{
    char fmt[260];
    max_len = 3;
    run("abcdef%$", NULL);
    ASSERT_STR("abc", out, out_len);
    max_len = 100;
    run("%12345678901d%$", NULL);
    ASSERT_STR("??", out, out_len);
    memset(fmt, 'x', sizeof(fmt));
    run(fmt, NULL);
    ASSERT_EQ(100, out_len);
    ASSERT_EQ('x', out[99]);
}

int main(void)
{
    printf("VFMT_$MAIN tests:\n");
    RUN_TEST(literal_and_end);
    RUN_TEST(decimal);
    RUN_TEST(word_signed);
    RUN_TEST(hex_width);
    RUN_TEST(octal_and_bad);
    RUN_TEST(string);
    RUN_TEST(string_compress_and_long_length);
    RUN_TEST(string_m_modifier);
    RUN_TEST(tab_x_repeat);
    RUN_TEST(limits);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
