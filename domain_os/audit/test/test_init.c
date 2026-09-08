/*
 * audit/test/test_init.c - unit tests for AUDIT_$INIT (0x00E70AFC).
 *
 * audit/init.c is #included below with GET_WIRED, ML_$EXCLUSION_INIT,
 * EC_$INIT, ACL_$ENTER_SUPER / ACL_$EXIT_SUPER, audit_$start_logging and
 * VFMT_$WRITE10 mocked.  The tests pin down bead source-3ti7: the three
 * `pea (d,PC)` cells the failure path pushes really are the image strings and
 * the 0x00E70C20 terminator (the C used to pass NULL), and 0x00E70BDC calls
 * ACL_$ENTER_SUPER a second time rather than ACL_$EXIT_SUPER.
 */

#include <stdio.h>
#include <string.h>
#include <stdarg.h>

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

#define ASSERT_TRUE(cond) do {                                           \
    if (!(cond)) {                                                       \
        printf("FAILED\n    %s at line %d\n", #cond, __LINE__);          \
        current_failed = 1;                                              \
        return;                                                          \
    }                                                                    \
} while (0)

#define ASSERT_STR_EQ(expected, actual) do {                             \
    if (strcmp((expected), (actual)) != 0) {                             \
        printf("FAILED\n    Expected \"%s\", got \"%s\" at line %d\n",   \
               (expected), (actual), __LINE__);                          \
        current_failed = 1;                                              \
        return;                                                          \
    }                                                                    \
} while (0)

#include "audit/audit_internal.h"
#include "acl/acl.h"
#include "misc/misc.h"

/* ------------------------------------------------------------------ */
/* Globals                                                             */
/* ------------------------------------------------------------------ */

audit_data_t AUDIT_$DATA;
int8_t       AUDIT_$ENABLED;
int8_t       AUDIT_$CORRUPTED;
uid_t        UID_$NIL = { 0, 0 };

/* ------------------------------------------------------------------ */
/* Mocks                                                               */
/* ------------------------------------------------------------------ */

static uint8_t wired_area[64];

void *GET_WIRED(void)
{
    return wired_area;
}

static int excl_init_calls;
static void *excl_init_arg;

void ML_$EXCLUSION_INIT(ml_$exclusion_t *excl)
{
    excl_init_calls++;
    excl_init_arg = excl;
}

static int   ec_init_calls;
static void *ec_init_arg;

void EC_$INIT(ec_$eventcount_t *ec)
{
    ec_init_calls++;
    ec_init_arg = ec;
}

static int enter_super_calls;
static int exit_super_calls;

void ACL_$ENTER_SUPER(void) { enter_super_calls++; }
void ACL_$EXIT_SUPER(void)  { exit_super_calls++; }

static status_$t start_logging_result;
static int       start_logging_calls;

void audit_$start_logging(status_$t *status_ret)
{
    start_logging_calls++;
    *status_ret = start_logging_result;
}

#define VFMT_MAX_CALLS 4
static int         vfmt_calls;
static const char *vfmt_fmt[VFMT_MAX_CALLS];
static const void *vfmt_a1[VFMT_MAX_CALLS];
static const void *vfmt_a2[VFMT_MAX_CALLS];

void VFMT_$WRITE10(const char *format, ...)
{
    va_list ap;

    if (vfmt_calls < VFMT_MAX_CALLS) {
        va_start(ap, format);
        vfmt_fmt[vfmt_calls] = format;
        vfmt_a1[vfmt_calls]  = va_arg(ap, const void *);
        vfmt_a2[vfmt_calls]  = va_arg(ap, const void *);
        va_end(ap);
    }
    vfmt_calls++;
}

#include "../init.c"

static void run_init(status_$t start_result)
{
    memset(&AUDIT_$DATA, 0xCC, sizeof(AUDIT_$DATA));
    memset(wired_area, 0, sizeof(wired_area));
    AUDIT_$ENABLED = (int8_t)0xFF;
    AUDIT_$CORRUPTED = (int8_t)0xFF;
    excl_init_calls = ec_init_calls = 0;
    excl_init_arg = ec_init_arg = NULL;
    enter_super_calls = exit_super_calls = 0;
    start_logging_calls = 0;
    start_logging_result = start_result;
    vfmt_calls = 0;
    memset(vfmt_fmt, 0, sizeof(vfmt_fmt));
    memset(vfmt_a1, 0, sizeof(vfmt_a1));
    memset(vfmt_a2, 0, sizeof(vfmt_a2));

    AUDIT_$INIT();
}

/*
 * The three `pea (d,PC)` cells, byte for byte from the image.
 *   0x00E70C40  25 2f 25 2f 25 2f 25 2f 57 61 72 ... 68 25 2e
 *   0x00E70C24  41 6c 6c 20 ... 65 64 2e 25 2e
 *   0x00E70BEA  4f 6e 6c 79 ... 69 6e 2e 25 2e
 *   0x00E70C20  00 00 00 00
 */
TEST(constant_cells)
{
    ASSERT_STR_EQ("%/%/%/%/Warning, could not start audit trail: 0x%8zulh%.",
                  audit_$init_warning_fmt);
    ASSERT_STR_EQ("All events will be logged.%.",
                  audit_$init_all_events_fmt);
    ASSERT_STR_EQ("Only audit administrators will be allowed to login.%.",
                  audit_$init_admins_only_fmt);
    ASSERT_EQ(0u, audit_$init_arg_end);

    /* 0x00E70C40 .. 0x00E70C77 inclusive is 0x38 bytes plus no terminator in
     * the image; the C array carries the same 56 characters. */
    ASSERT_EQ(56, sizeof(audit_$init_warning_fmt) - 1);
    /* 0x00E70C24 .. 0x00E70C3F is 28 bytes. */
    ASSERT_EQ(28, sizeof(audit_$init_all_events_fmt) - 1);
    /* 0x00E70BEA .. 0x00E70C1E is 53 bytes. */
    ASSERT_EQ(53, sizeof(audit_$init_admins_only_fmt) - 1);
}

/* Happy path: nothing is printed and CORRUPTED is left clear. */
TEST(successful_start_prints_nothing)
{
    run_init(status_$ok);

    ASSERT_EQ(1, start_logging_calls);
    ASSERT_EQ(0, vfmt_calls);
    ASSERT_EQ(0, AUDIT_$CORRUPTED);      /* 0x00E70B18: clr.b, never set */
    ASSERT_EQ(0, AUDIT_$ENABLED);        /* 0x00E70B12: clr.b */
}

/* 0x00E70B98-0x00E70BD4: three calls, with the image cells as arguments. */
TEST(failed_start_prints_three_messages)
{
    run_init(0x00300001);

    ASSERT_EQ(3, vfmt_calls);

    ASSERT_TRUE(vfmt_fmt[0] == audit_$init_warning_fmt);
    ASSERT_TRUE(vfmt_a1[0] != NULL);            /* &status, not NULL */
    ASSERT_TRUE(vfmt_a2[0] == &audit_$init_arg_end);

    ASSERT_TRUE(vfmt_fmt[1] == audit_$init_all_events_fmt);
    ASSERT_TRUE(vfmt_a1[1] == &audit_$init_arg_end);
    ASSERT_TRUE(vfmt_a2[1] == &audit_$init_arg_end);

    ASSERT_TRUE(vfmt_fmt[2] == audit_$init_admins_only_fmt);
    ASSERT_TRUE(vfmt_a1[2] == &audit_$init_arg_end);
    ASSERT_TRUE(vfmt_a2[2] == &audit_$init_arg_end);

    /* 0x00E70BD6: st */
    ASSERT_EQ((int8_t)-1, AUDIT_$CORRUPTED);
}

/*
 * 0x00E70B82 and 0x00E70BDC both `jsr 0x00E46F90` - ACL_$ENTER_SUPER.  The
 * image never calls ACL_$EXIT_SUPER (0x00E46FB4) here.
 */
TEST(enter_super_is_called_twice_and_exit_never)
{
    run_init(status_$ok);
    ASSERT_EQ(2, enter_super_calls);
    ASSERT_EQ(0, exit_super_calls);

    run_init(0x00300001);
    ASSERT_EQ(2, enter_super_calls);
    ASSERT_EQ(0, exit_super_calls);
}

/* 0x00E70B0E / 0x00E70B66 / 0x00E70B76: the wired block carries both. */
TEST(wired_block_holds_eventcount_and_exclusion)
{
    run_init(status_$ok);

    ASSERT_TRUE((void *)AUDIT_$DATA.event_count == (void *)wired_area);
    ASSERT_EQ(1, ec_init_calls);
    ASSERT_TRUE(ec_init_arg == (void *)wired_area);
    ASSERT_EQ(1, excl_init_calls);
    ASSERT_TRUE(excl_init_arg == (void *)(wired_area + 0x0C));
}

/* 0x00E70B22-0x00E70B64: the cleared fields. */
TEST(clears_the_data_area)
{
    int i;

    run_init(status_$ok);

    ASSERT_EQ(0u, AUDIT_$DATA.list_uid.high);
    ASSERT_EQ(0u, AUDIT_$DATA.list_uid.low);
    ASSERT_EQ(0u, AUDIT_$DATA.log_file_uid.high);
    ASSERT_EQ(0u, AUDIT_$DATA.log_file_uid.low);
    ASSERT_EQ(0, AUDIT_$DATA.list_count);
    ASSERT_EQ(0, AUDIT_$DATA.flags);
    ASSERT_TRUE(AUDIT_$DATA.buffer_base == NULL);
    ASSERT_EQ(0u, AUDIT_$DATA.buffer_size);
    ASSERT_EQ(0, AUDIT_$DATA.server_pid);
    ASSERT_EQ(0u, AUDIT_$DATA.lock_id);
    ASSERT_EQ(0, AUDIT_$DATA.server_running);

    /* 0x00E70B4E-0x00E70B5C: `moveq #0x3f` + `dbf` clears 64 words. */
    for (i = 0; i < AUDIT_MAX_PROCESSES; i++) {
        ASSERT_EQ(0, AUDIT_$DATA.suspend_count[i]);
    }
}

int main(void)
{
    printf("AUDIT_$INIT tests\n");

    RUN_TEST(constant_cells);
    RUN_TEST(successful_start_prints_nothing);
    RUN_TEST(failed_start_prints_three_messages);
    RUN_TEST(enter_super_is_called_twice_and_exit_never);
    RUN_TEST(wired_block_holds_eventcount_and_exclusion);
    RUN_TEST(clears_the_data_area);

    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
