/*
 * misc/test/test_start_mem_lites.c - START_MEM_LITES (0x00E0C43C): the
 * light rows from the display height, the process creation and the
 * failure message.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>

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
        printf("FAILED\n    Expected: 0x%lx, Got: 0x%lx at line %d\n", \
               _e, _a, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while (0)

#include <stdarg.h>
#include "misc/misc_internal.h"
#include "smd/smd.h"
#include "proc1/proc1.h"

uint16_t mem_lites_row2, mem_lites_row1;
void MEM_LITES(void) { }

static status_$t create_status;
static void *c_fn;
static uint32_t c_type;
static int n_write;
static const char *w_fmt;
static status_$t *w_st;
static const uint32_t *w_zero;
static uint16_t inq_unit;

void SMD_$INQ_DISP_INFO(uint16_t *unit, smd_disp_info_result_t *info, status_$t *status_ret)
{
    inq_unit = *unit;
    info->height = 0x320;
    *status_ret = 0;
}

uint16_t PROC1_$CREATE_P(void *funcptr, uint32_t type, status_$t *status_ret)
{
    c_fn = funcptr; c_type = type;
    *status_ret = create_status;
    return 3;
}

void VFMT_$WRITE10(const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    w_fmt = format;
    w_st = va_arg(ap, status_$t *);
    w_zero = va_arg(ap, const uint32_t *);
    va_end(ap);
    n_write++;
}

#include "../start_mem_lites.c"

TEST(ok)
{
    create_status = 0; n_write = 0;
    START_MEM_LITES();
    ASSERT_EQ(1, inq_unit);
    ASSERT_EQ(0x320 - 0x30, mem_lites_row1);
    ASSERT_EQ(0x320 - 0x10, mem_lites_row2);
    ASSERT_EQ(1, c_fn == (void *)MEM_LITES);
    ASSERT_EQ(0x0400000A, c_type);
    ASSERT_EQ(0, n_write);
}

TEST(failure_message)
{
    create_status = 0x00140003; n_write = 0;
    START_MEM_LITES();
    ASSERT_EQ(1, n_write);
    ASSERT_EQ(0, memcmp(w_fmt, "proc1_$create_p failed. status = %LH%.", 38));
    ASSERT_EQ(0x00140003, *w_st);
    ASSERT_EQ(0, *w_zero);
}

int main(void)
{
    printf("START_MEM_LITES tests:\n");
    RUN_TEST(ok);
    RUN_TEST(failure_message);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
