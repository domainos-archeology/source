/*
 * mst/test/test_diskless_init.c - unit tests for MST_$DISKLESS_INIT (0x00E30DA8).
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int tests_passed = 0;
static int tests_failed = 0;
static int current_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do {                     \
    printf("  Running %s... ", #name);          \
    current_failed = 0;                         \
    test_##name();                              \
    if (current_failed) { tests_failed++; }     \
    else { tests_passed++; printf("PASSED\n"); }\
} while (0)

#define ASSERT_EQ(expected, actual) do {                                 \
    unsigned long long _e = (unsigned long long)(expected);              \
    unsigned long long _a = (unsigned long long)(actual);                \
    if (_e != _a) {                                                      \
        printf("FAILED\n    Expected 0x%llx, got 0x%llx at line %d\n",   \
               _e, _a, __LINE__);                                        \
        current_failed = 1;                                              \
        return;                                                          \
    }                                                                    \
} while (0)

#include <stdarg.h>
#include "mst/mst_internal.h"
#include "vfmt/vfmt.h"

char MST_$DISKLESS_MSG[MST_DISKLESS_MSG_SIZE];
boolean MST_$GOT_COLOR;

static const char *v_fmt;
static char *v_buf;
static int16_t v_max;
static uint32_t v_a1, v_a2;

void VFMT_$FORMATN(const char *format, char *buf, int16_t *max_len,
                   int16_t *out_len, ...)
{
    va_list ap;
    int i;
    va_start(ap, out_len);
    v_a1 = *va_arg(ap, uint32_t *);
    v_a2 = *va_arg(ap, uint32_t *);
    va_end(ap);
    v_fmt = format; v_buf = buf; v_max = *max_len;
    /* copy the literal text up to the first '%' */
    for (i = 0; format[i] != '%'; i++) buf[i] = format[i];
    *out_len = (int16_t)i;
}

#include "../diskless_init.c"

TEST(formats_and_patches)
{
    memset(MST_$DISKLESS_MSG, 0, sizeof(MST_$DISKLESS_MSG));
    MST_$GOT_COLOR = 0;
    MST_$DISKLESS_INIT((boolean)-1, 0x1234, 0x5678);
    ASSERT_EQ((uintptr_t)MST_$DISKLESS_MSG, (uintptr_t)v_buf);
    ASSERT_EQ(0x79, v_max);
    ASSERT_EQ(0x5678, v_a1);            /* node_me first */
    ASSERT_EQ(0x1234, v_a2);
    ASSERT_EQ(0, memcmp(MST_$DISKLESS_MSG, "PARTNER NOT RESPONDING", 22));
    ASSERT_EQ('.', MST_$DISKLESS_MSG[0x4E]);
    ASSERT_EQ('\r', MST_$DISKLESS_MSG[0x4F]);
    ASSERT_EQ('\n', MST_$DISKLESS_MSG[0x50]);
    ASSERT_EQ('\r', MST_$DISKLESS_MSG[0x51]);
    ASSERT_EQ('M', MST_$DISKLESS_MSG[0x52]);
    ASSERT_EQ(0xFF, (uint8_t)MST_$GOT_COLOR);

    MST_$DISKLESS_INIT(0, 1, 2);
    ASSERT_EQ(0, (uint8_t)MST_$GOT_COLOR);
    ASSERT_EQ(0x70, strlen(v_fmt));
    ASSERT_EQ('$', v_fmt[0x6F]);
}

int main(void)
{
    printf("MST_$DISKLESS_INIT tests\n");
    RUN_TEST(formats_and_patches);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
