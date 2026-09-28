/*
 * Tests for dtty/helpers.c: dtty_$report_error (0x00E1D5B2),
 * dtty_$clear_window (0x00E1D592), dtty_$load_font (0x00E1D668),
 * dtty_$get_disp_type (0x00E1D588).  Pinned: the exact VFMT call
 * sequence and format cells, the '$' context short-circuit, the status
 * being passed by address, and clear_window zeroing the status first.
 */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "base/base.h"
#include "dtty/dtty_internal.h"
#include "vfmt/vfmt.h"

int __host_intr_disable_count = 0;
uint16_t DTTY_$DISP_TYPE;
static int n_write; static const char *fmts[8]; static const void *arg2s[8];
void VFMT_$WRITE10(const char *f, ...)
{
    va_list ap; va_start(ap, f);
    if (n_write < 8) { fmts[n_write] = f; arg2s[n_write] = va_arg(ap, const void *); }
    va_end(ap); n_write++;
}
static int n_clear, n_copy; static status_$t *clear_status_seen; static status_$t clear_status_val;
void SMD_$CLEAR_WINDOW(smd_rect_t *r, status_$t *s) { (void)r; n_clear++; clear_status_seen = s; clear_status_val = *s; *s = 0x77; }
void SMD_$COPY_FONT_TO_MD_HDM(void **f, status_$t *s) { (void)f; n_copy++; *s = 0x55; }

#include "dtty/helpers.c"

static int tests_run, tests_failed;
#define TEST(name) static void name(void)
#define RUN_TEST(name) do { tests_run++; n_write = n_clear = n_copy = 0; name(); } while (0)
#define ASSERT_EQ(a, b) do { long long _a = (long long)(a), _b = (long long)(b); \
    if (_a != _b) { printf("  FAIL %s:%d: %s == %lld, expected %s == %lld\n", __FILE__, __LINE__, #a, _a, #b, _b); \
    tests_failed++; return; } } while (0)

TEST(report_error_full)
{
    dtty_$report_error(0x00120003, "smd_$assoc%", "loading");
    ASSERT_EQ(n_write, 5);
    ASSERT_EQ(strcmp(fmts[0], "%/Error status %h returned from %$"), 0);
    ASSERT_EQ(*(const status_$t *)arg2s[0], 0x00120003);
    ASSERT_EQ(strcmp(fmts[1], "smd_$assoc%"), 0);
    ASSERT_EQ(arg2s[1] == &dtty_$zero_00e1d664, 1);
    ASSERT_EQ(strcmp(fmts[2], " while attempting to%."), 0);
    ASSERT_EQ(strcmp(fmts[3], "loading"), 0);
    ASSERT_EQ(strcmp(fmts[4], "%."), 0);
}

TEST(report_error_dollar_context)
{
    dtty_$report_error(1, "f", "$");
    ASSERT_EQ(n_write, 3); ASSERT_EQ(strcmp(fmts[2], "%."), 0);
}

TEST(clear_window_and_load_font)
{
    status_$t st = 0x11; smd_rect_t r = { 0, 0, 0, 0 }; void *font = NULL;
    dtty_$clear_window(&r, &st);
    ASSERT_EQ(n_clear, 1); ASSERT_EQ(clear_status_seen == &st, 1); ASSERT_EQ(clear_status_val, 0); ASSERT_EQ(st, 0x77);
    DTTY_$DISP_TYPE = 2;
    dtty_$load_font(&font, &st);
    ASSERT_EQ(n_copy, 1); ASSERT_EQ(st, 0x55); ASSERT_EQ(dtty_$get_disp_type(), 2);
}

int main(void)
{
    RUN_TEST(report_error_full); RUN_TEST(report_error_dollar_context); RUN_TEST(clear_window_and_load_font);
    printf("%s: %d tests, %d failed\n", __FILE__, tests_run, tests_failed);
    return tests_failed != 0;
}
