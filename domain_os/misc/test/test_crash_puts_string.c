/*
 * misc/test/test_crash_puts_string.c - Unit tests for the crash console
 *
 * Self-contained host program: includes misc/crash_system.c directly (which
 * carries the portable model of crash_puts_string at 0x00E1E7C8 and the real
 * CRASH_REPORT template block) and supplies crash_putc, so every character
 * the formatter emits is captured.
 *
 * The interesting cases all come from the template at 0x00E1E97E, which is
 * the only crash string in the image that uses every format byte:
 *   "\r\n" "Crash_Status " 0xFF <long> "  PC " 0xFF <long> " pid " 0x00 <word> '%'
 */

#include "time/time.h"     /* TIME_$CLOCKH_EC / TIME_$CLOCKH; before <stdio.h> (base.h uid_t) */
#include <stdio.h>
#include <string.h>

#include "misc/misc_internal.h"

/* ==========================================================================
 * Test framework
 * ========================================================================== */

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
        printf("FAILED\n    Expected: 0x%lx (%lu), Got: 0x%lx (%lu) at line %d\n", \
               _e, _e, _a, _a, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while (0)

#define ASSERT_STR(expected, actual) do { \
    if (strcmp((expected), (actual)) != 0) { \
        printf("FAILED\n    Expected: \"%s\"\n    Got:      \"%s\"  at line %d\n", \
               (expected), (actual), __LINE__); \
        tests_failed++; \
        return; \
    } \
} while (0)

/* ==========================================================================
 * Console capture + the globals the model references
 * ========================================================================== */

static char console[512];
static size_t console_len;

void crash_putc(char c)
{
    if (console_len + 1 < sizeof(console)) {
        console[console_len++] = c;
        console[console_len] = '\0';
    }
}

static void console_reset(void) { console_len = 0; console[0] = '\0'; }

/* Referenced by the CRASH_SYSTEM model in crash_system.c */
uint16_t PROC1_$CURRENT;
ec_$eventcount_t TIME_$CLOCKH_EC = { .value = (int32_t)(0) };  /* TIME_$CLOCKH = its value */

/* Code under test */
#include "../crash_system.c"

/* ==========================================================================
 * Tests
 * ========================================================================== */

/*
 * The recovered layout must place each live field exactly where the template
 * expects its format byte, or the message comes out garbled.
 */
TEST(report_block_layout)
{
    ASSERT_EQ(0x10, __builtin_offsetof(crash_report_t, status));
    ASSERT_EQ(0x1A, __builtin_offsetof(crash_report_t, pc));
    ASSERT_EQ(0x24, __builtin_offsetof(crash_report_t, pid));
    ASSERT_EQ(0x26, __builtin_offsetof(crash_report_t, terminator));
    ASSERT_EQ(0x28, __builtin_offsetof(crash_report_t, regs));
    ASSERT_EQ(0x6C, __builtin_offsetof(crash_report_t, usp));
    ASSERT_EQ(0x70, sizeof(crash_report_t));
}

/* The template bytes themselves, as read out of the image at 0x00E1E97E. */
TEST(report_block_bytes)
{
    static const uint8_t image[0x28] = {
        0x0d, 0x0a, 0x43, 0x72, 0x61, 0x73, 0x68, 0x5f,
        0x53, 0x74, 0x61, 0x74, 0x75, 0x73, 0x20, 0xff,
        0x00, 0x00, 0x00, 0x00, 0x20, 0x20, 0x50, 0x43,
        0x20, 0xff, 0x00, 0x00, 0x00, 0x00, 0x20, 0x70,
        0x69, 0x64, 0x20, 0x00, 0x00, 0x00, 0x25, 0x00
    };
    const uint8_t *ours = (const uint8_t *)&CRASH_REPORT;
    size_t i;

    CRASH_REPORT.status = 0;
    CRASH_REPORT.pc = 0;
    CRASH_REPORT.pid = 0;

    for (i = 0; i < sizeof(image); i++) {
        if (ours[i] != image[i]) {
            printf("FAILED\n    byte +0x%02zx = 0x%02x, image has 0x%02x\n",
                   i, ours[i], image[i]);
            tests_failed++;
            return;
        }
    }
}

/* Plain characters pass through; '%' ends the string with CR LF. */
TEST(literal_text_and_terminator)
{
    console_reset();
    crash_puts_string("hello%");
    ASSERT_STR("hello\r\n", console);
}

/* A negative format byte introduces an 8-digit big-endian hex field. */
TEST(long_hex_field)
{
    static const char s[] = { (char)0xff, 0x12, 0x34, (char)0xab, (char)0xcd, '%' };
    console_reset();
    crash_puts_string(s);
    ASSERT_STR("1234ABCD\r\n", console);
}

/* A 0x00 format byte introduces a 4-digit hex field. */
TEST(word_hex_field)
{
    static const char s[] = { 0x00, (char)0xbe, (char)0xef, '%' };
    console_reset();
    crash_puts_string(s);
    ASSERT_STR("BEEF\r\n", console);
}

/* 0..9 use '0'+n and 10..15 use the addq.b #7 path at 0x00E1E804. */
TEST(hex_digit_mapping)
{
    static const char s[] = { (char)0x80, 0x01, 0x23, 0x45, 0x67, '%' };
    static const char t[] = { (char)0x80, (char)0x89, (char)0xab,
                              (char)0xcd, (char)0xef, '%' };
    console_reset();
    crash_puts_string(s);
    ASSERT_STR("01234567\r\n", console);

    console_reset();
    crash_puts_string(t);
    ASSERT_STR("89ABCDEF\r\n", console);
}

/* Fields and literal text interleave, and the scan resumes after each field. */
TEST(mixed_fields)
{
    static const char s[] = { 'a', 0x00, 0x00, 0x01, 'b',
                              (char)0xff, 0x00, 0x00, 0x00, 0x02, 'c', '%' };
    console_reset();
    crash_puts_string(s);
    ASSERT_STR("a0001b00000002c\r\n", console);
}

/* The real template, rendered with live values. */
TEST(full_crash_report)
{
    console_reset();
    CRASH_REPORT.status = BE32_CONST(0x000B0008u);
    CRASH_REPORT.pc = BE32_CONST(0x00E1BA9Cu);
    CRASH_REPORT.pid = BE16_CONST(0x0007u);
    crash_puts_string((const char *)&CRASH_REPORT);
    ASSERT_STR("\r\nCrash_Status 000B0008  PC 00E1BA9C pid 0007\r\n", console);
}

/*
 * CRASH_SYSTEM prints nothing for a clean shutdown (status 0) or a clean
 * reboot (0x001B0008); anything else fills in the live fields and prints.
 */
TEST(crash_system_message_gating)
{
    status_$t st;

    console_reset();
    st = status_$ok;
    CRASH_SYSTEM(&st);
    ASSERT_STR("", console);
    ASSERT_EQ(0, BE32_CONST(CRASH_REPORT.status));

    console_reset();
    st = status_$system_reboot;
    CRASH_SYSTEM(&st);
    ASSERT_STR("", console);
    ASSERT_EQ(status_$system_reboot, BE32_CONST(CRASH_REPORT.status));

    console_reset();
    PROC1_$CURRENT = 0x1234;
    st = 0x00040005;
    CRASH_SYSTEM(&st);
    ASSERT_EQ(0x00040005, BE32_CONST(CRASH_REPORT.status));
    ASSERT_EQ(0x1234, BE16_CONST(CRASH_REPORT.pid));
    /* The pc is the caller's return address, so only check the fixed text. */
    if (strncmp(console, "\r\nCrash_Status 00040005  PC ", 28) != 0) {
        printf("FAILED\n    got \"%s\"\n", console);
        tests_failed++;
        return;
    }
    ASSERT_STR(" pid 1234\r\n", console + 28 + 8);
}

/* CRASH_SHOW_STRING is the same formatter with the registers preserved. */
TEST(crash_show_string)
{
    console_reset();
    CRASH_SHOW_STRING("Shutdown successful.%");
    ASSERT_STR("Shutdown successful.\r\n", console);
}

/* ==========================================================================
 * Main
 * ========================================================================== */

int main(void)
{
    printf("crash console tests\n");

    RUN_TEST(report_block_layout);
    RUN_TEST(report_block_bytes);
    RUN_TEST(literal_text_and_terminator);
    RUN_TEST(long_hex_field);
    RUN_TEST(word_hex_field);
    RUN_TEST(hex_digit_mapping);
    RUN_TEST(mixed_fields);
    RUN_TEST(full_crash_report);
    RUN_TEST(crash_system_message_gating);
    RUN_TEST(crash_show_string);

    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
