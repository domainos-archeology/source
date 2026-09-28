/*
 * os/test/test_boot_errchk.c - unit tests for OS_$BOOT_ERRCHK (0x00E34B14)
 *
 * Mocks VFMT_$FORMATN, CRASH_SHOW_STRING and TIME_$WAIT to check the
 * low-word status test, the '%' search and the by-reference arguments.
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define RUN_TEST(name) do { \
    printf("  Running %s... ", #name); \
    reset_state(); \
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

#include "os/os_internal.h"
#include "misc/crash_system.h"
#include "time/time.h"
#include "vfmt/vfmt.h"

static int         mock_fmt_calls;
static const char *mock_fmt_format;
static int16_t     mock_fmt_max;
static const char *mock_fmt_msg;
static int32_t     mock_fmt_msg_len;
static const char *mock_fmt_arg;
static int32_t     mock_fmt_arg_len;
static status_$t   mock_fmt_status;
static int         mock_show_calls;
static char        mock_show_text[100];
static int         mock_wait_calls;
static uint16_t    mock_wait_type;
static clock_t     mock_wait_clock;

void VFMT_$FORMATN(const char *format, char *buf, int16_t *max_len,
                   int16_t *out_len, ...)
{
    va_list ap;
    va_start(ap, out_len);
    mock_fmt_calls++;
    mock_fmt_format = format;
    mock_fmt_max = *max_len;
    mock_fmt_msg = va_arg(ap, const char *);
    mock_fmt_msg_len = *va_arg(ap, int32_t *);
    mock_fmt_arg = va_arg(ap, const char *);
    mock_fmt_arg_len = *va_arg(ap, int32_t *);
    mock_fmt_status = *va_arg(ap, status_$t *);
    va_end(ap);
    memcpy(buf, "LINE", 4);
    *out_len = 4;
}

void CRASH_SHOW_STRING(const char *str)
{
    mock_show_calls++;
    memcpy(mock_show_text, str, 4);
}

void TIME_$WAIT(uint16_t *delay_type, clock_t *delay, status_$t *status)
{
    mock_wait_calls++;
    mock_wait_type = *delay_type;
    mock_wait_clock = *delay;
    *status = status_$ok;
}

#include "os/boot_errchk.c"

static void reset_state(void)
{
    mock_fmt_calls = 0;
    mock_show_calls = 0;
    mock_wait_calls = 0;
    memset(mock_show_text, 0, sizeof(mock_show_text));
}

/* a status whose low word is zero is "no error", whatever its high word */
static void test_low_word_zero_is_ok(void)
{
    status_$t st = 0x00120000;
    short len = 3;
    ASSERT_EQ(0xFF, (uint8_t)OS_$BOOT_ERRCHK("msg", "arg", &len, &st));
    ASSERT_EQ(0, mock_fmt_calls);
    ASSERT_EQ(0, mock_show_calls);
    ASSERT_EQ(0, mock_wait_calls);
}

/* an error: formatted, shown, waited on, false */
static void test_error_path(void)
{
    status_$t st = 0x00080007;
    short len = 3;
    ASSERT_EQ(0, (uint8_t)OS_$BOOT_ERRCHK("boot % failed", "arg", &len, &st));
    ASSERT_EQ(1, mock_fmt_calls);
    ASSERT_EQ(100, mock_fmt_max);
    ASSERT_EQ(0, strcmp("%/%/%a %a -- %lh%/%%%$", mock_fmt_format));
    ASSERT_EQ(5, mock_fmt_msg_len);                  /* index of the '%' */
    ASSERT_EQ(3, mock_fmt_arg_len);
    ASSERT_EQ(0x00080007, mock_fmt_status);
    ASSERT_EQ(1, mock_show_calls);
    ASSERT_EQ(0, memcmp("LINE", mock_show_text, 4));
    ASSERT_EQ(1, mock_wait_calls);
    ASSERT_EQ(0, mock_wait_type);
    ASSERT_EQ(5, mock_wait_clock.high);              /* the 0xE35374 cell */
    ASSERT_EQ(0, mock_wait_clock.low);
}

/* no '%' in the first 50 characters: the length stays 0 */
static void test_no_percent(void)
{
    status_$t st = 1;
    short len = 0;
    char msg[64];
    memset(msg, 'a', sizeof(msg));
    msg[55] = '%';
    (void)OS_$BOOT_ERRCHK(msg, "", &len, &st);
    ASSERT_EQ(0, mock_fmt_msg_len);
    memset(msg, 'a', sizeof(msg));
    msg[49] = '%';
    (void)OS_$BOOT_ERRCHK(msg, "", &len, &st);
    ASSERT_EQ(49, mock_fmt_msg_len);
}

int main(void)
{
    printf("OS_$BOOT_ERRCHK tests:\n");
    RUN_TEST(low_word_zero_is_ok);
    RUN_TEST(error_path);
    RUN_TEST(no_percent);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
