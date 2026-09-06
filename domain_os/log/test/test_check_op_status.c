/*
 * log/test/test_check_op_status.c - Unit tests for log_$check_op_status
 *
 * Covers bead source-5pu: the nested procedure's uplevel status variable is
 * now an explicit "status_$t *" parameter instead of a file-scope copy, and
 * the address that is handed to ERROR_$PRINT is the caller's own variable
 * (0xE2FF8E "pea (-0x44,A2)").
 *
 * Also pins the two things the previous version got wrong:
 *   - only the HIGH word of the status is tested (0xE2FF84 "tst.w (-0x42,A2)")
 *   - three 3-argument ERROR_$PRINT calls are made, the middle one using the
 *     operation name itself as the continuation format with two copies of
 *     LOG_$VFMT_NO_ARG.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define RUN_TEST(name) do { \
    printf("  Running %s... ", #name); \
    reset_mocks(); \
    test_##name(); \
    printf("PASSED\n"); \
    tests_passed++; \
} while (0)

#define ASSERT_EQ(expected, actual) do { \
    if ((long)(expected) != (long)(actual)) { \
        printf("FAILED\n    Expected: %ld, Got: %ld at line %d\n", \
               (long)(expected), (long)(actual), __LINE__); \
        tests_failed++; \
        return; \
    } \
} while (0)

#define ASSERT_TRUE(cond) do { \
    if (!(cond)) { \
        printf("FAILED\n    Assertion failed at line %d: %s\n", __LINE__, #cond); \
        tests_failed++; \
        return; \
    } \
} while (0)

#include "log/log_internal.h"

/* Backing store for the shared zero-argument sentinel (0x00e2fffc). */
uint32_t LOG_$VFMT_NO_ARG = 0;

/*
 * ERROR_$PRINT mock.  The real routine is a thunk to VFMT_$WRITEN taking a
 * format plus two by-reference arguments; record all three.
 */
#define MAX_PRINTS 8
static int print_count;
static const void *print_args[MAX_PRINTS][3];

void ERROR_$PRINT(const char *format, ...)
{
    va_list ap;
    if (print_count < MAX_PRINTS) {
        print_args[print_count][0] = format;
        va_start(ap, format);
        print_args[print_count][1] = va_arg(ap, const void *);
        print_args[print_count][2] = va_arg(ap, const void *);
        va_end(ap);
    }
    print_count++;
}

static void reset_mocks(void)
{
    print_count = 0;
    memset(print_args, 0, sizeof(print_args));
    LOG_$VFMT_NO_ARG = 0;
}

/* A zero status prints nothing and returns 0. */
static void test_success_is_silent(void)
{
    status_$t status = 0;
    ASSERT_EQ(0, log_$check_op_status("map%$", &status));
    ASSERT_EQ(0, print_count);
}

/*
 * Only the HIGH word is tested.  A status whose high word is zero but whose
 * low word is not is still "success" - the previous code that compared the
 * whole 32-bit word would have reported an error here.
 */
static void test_only_high_word_is_tested(void)
{
    status_$t status = 0x0000FFFF;
    ASSERT_EQ(0, log_$check_op_status("map%$", &status));
    ASSERT_EQ(0, print_count);

    status = 0x000E0007;                /* status_$naming_name_not_found */
    ASSERT_EQ((int8_t)-1, log_$check_op_status("map%$", &status));
    ASSERT_EQ(3, print_count);
}

/*
 * The failure path emits exactly three continued ERROR_$PRINT calls, and the
 * first one hands out the ADDRESS of the caller's own status variable.
 */
static void test_failure_message_shape(void)
{
    status_$t status = 0x00070001;
    ASSERT_EQ((int8_t)-1, log_$check_op_status("get_attributes%$", &status));
    ASSERT_EQ(3, print_count);

    /* Call 1: "%/%/Warning: Status %lh, Unable to %$", &status, &no_arg */
    ASSERT_TRUE(strcmp((const char *)print_args[0][0],
                       "%/%/Warning: Status %lh, Unable to %$") == 0);
    ASSERT_TRUE(print_args[0][1] == (const void *)&status);
    ASSERT_TRUE(print_args[0][2] == (const void *)&LOG_$VFMT_NO_ARG);

    /* Call 2: the operation name is itself the continuation format. */
    ASSERT_TRUE(strcmp((const char *)print_args[1][0], "get_attributes%$") == 0);
    ASSERT_TRUE(print_args[1][1] == (const void *)&LOG_$VFMT_NO_ARG);
    ASSERT_TRUE(print_args[1][2] == (const void *)&LOG_$VFMT_NO_ARG);

    /* Call 3: closes the message with the pathname and its length (36). */
    ASSERT_TRUE(strcmp((const char *)print_args[2][0],
                       " %a -- error logging disabled.%.") == 0);
    ASSERT_TRUE(strcmp((const char *)print_args[2][1],
                       "`node_data/system_logs/sys_error_log") == 0);
    ASSERT_EQ(36, *(const int32_t *)print_args[2][2]);
    ASSERT_EQ(36, (int)strlen((const char *)print_args[2][1]));
}

/* The status variable itself is never modified. */
static void test_status_not_clobbered(void)
{
    status_$t status = 0x00120035;
    (void)log_$check_op_status("wire%$", &status);
    ASSERT_EQ(0x00120035, status);
}

int main(void)
{
    printf("Running log_$check_op_status tests...\n\n");

    RUN_TEST(success_is_silent);
    RUN_TEST(only_high_word_is_tested);
    RUN_TEST(failure_message_shape);
    RUN_TEST(status_not_clobbered);

    printf("\n%d tests passed, %d tests failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}

/* The real implementation under test. */
#include "../check_op_status.c"
