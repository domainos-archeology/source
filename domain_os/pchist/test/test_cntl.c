/*
 * pchist/test/test_cntl.c - PCHIST_$CNTL (0x00E5CDB6)
 *
 * PCHIST_$STOP_PROFILING, PCHIST_$UNWIRE_CLEANUP, PCHIST_$ENABLE_TERMINAL,
 * PROC2_$UPID_TO_UID, PROC2_$GET_PID, MST_$WIRE_AREA, ML_$EXCLUSION_* and
 * M$MIU$LLW are mocked.  Pins the range arithmetic (0x00E5CDE6-0x00E5CE44),
 * the PID word being the HIGH half of the third range longword
 * (0x00E5CE6C `tst.w (0x8,A3)`), the UPID conversion path, the histogram
 * fields written, the wire-area call and the command-0 count bump.
 */

#include <stdio.h>
#include <string.h>

#include "pchist/pchist_internal.h"
#include "mst/mst.h"
#include "math/math.h"

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    printf("  Running %-44s ", #name); \
    test_##name(); \
    tests_passed++; \
    printf("PASSED\n"); \
} while (0)

#define ASSERT_EQ(expected, actual) do { \
    unsigned long long _e = (unsigned long long)(expected); \
    unsigned long long _a = (unsigned long long)(actual); \
    if (_e != _a) { \
        printf("FAILED\n    Expected: 0x%llx, Got: 0x%llx at line %d\n", \
               _e, _a, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while (0)

/* ==========================================================================
 * Module data (pchist_data.c is not included: the layout is all the test
 * needs)
 * ========================================================================== */

pchist_control_t   PCHIST_$CONTROL;
pchist_proc_t      PCHIST_$PROC_DATA[PCHIST_MAX_PROCESSES];
pchist_histogram_t PCHIST_$HISTOGRAM;
uint32_t           PCHIST_$WIRE_PAGES[4];
int16_t            PCHIST_$WIRED_COUNT;
uint16_t           PROC1_$CURRENT;
uint16_t           PROC1_$AS_ID;

/* ==========================================================================
 * Mocks
 * ========================================================================== */

static int      stop_calls;
static int16_t *stop_cmd_ptr;
void PCHIST_$STOP_PROFILING(int16_t *cmd_ptr)
{
    stop_calls++;
    stop_cmd_ptr = cmd_ptr;
}

static int unwire_calls;
void PCHIST_$UNWIRE_CLEANUP(void)
{
    unwire_calls++;
}

static int     enable_calls;
static int16_t enable_arg;
void PCHIST_$ENABLE_TERMINAL(int16_t disabling)
{
    enable_calls++;
    enable_arg = disabling;
}

static int       upid_calls;
static int16_t   upid_seen;
static status_$t upid_status;
void PROC2_$UPID_TO_UID(int16_t *upid, uid_t *uid_ret, status_$t *status_ret)
{
    upid_calls++;
    upid_seen = *upid;
    uid_ret->high = 0x11111111;
    uid_ret->low = 0x22222222;
    *status_ret = upid_status;
}

static int       get_pid_calls;
static uint32_t  get_pid_uid_high;
static uint16_t  get_pid_result;
static status_$t get_pid_status;
uint16_t PROC2_$GET_PID(uid_t *proc_uid, status_$t *status_ret)
{
    get_pid_calls++;
    get_pid_uid_high = proc_uid->high;
    *status_ret = get_pid_status;
    return get_pid_result;
}

static int         wire_calls;
static uint32_t    wire_start, wire_end;
static const void *wire_pages;
static int16_t     wire_max;
static const void *wire_count_ret;
void MST_$WIRE_AREA(const void *start_va_ptr, const void *end_va_ptr,
                    void *page_list, const void *max_pages_ptr,
                    void *page_count_ret)
{
    wire_calls++;
    wire_start = *(const uint32_t *)start_va_ptr;
    wire_end = *(const uint32_t *)end_va_ptr;
    wire_pages = page_list;
    wire_max = *(const int16_t *)max_pages_ptr;
    wire_count_ret = page_count_ret;
}

static int excl_start_calls, excl_stop_calls;
void ML_$EXCLUSION_START(ml_$exclusion_t *excl) { (void)excl; excl_start_calls++; }
void ML_$EXCLUSION_STOP(ml_$exclusion_t *excl)  { (void)excl; excl_stop_calls++; }

ulong M$MIU$LLW(ulong multiplicand, ushort multiplier)
{
    return multiplicand * multiplier;
}

/* ==========================================================================
 * Code under test
 * ========================================================================== */

#include "../cntl.c"

static uint32_t  out[0x10B];
static status_$t status;

static void reset(void)
{
    memset(&PCHIST_$CONTROL, 0, sizeof(PCHIST_$CONTROL));
    memset(&PCHIST_$HISTOGRAM, 0x5A, sizeof(PCHIST_$HISTOGRAM));
    memset(out, 0, sizeof(out));
    stop_calls = unwire_calls = enable_calls = 0;
    upid_calls = get_pid_calls = wire_calls = 0;
    excl_start_calls = excl_stop_calls = 0;
    upid_status = status_$ok;
    get_pid_status = status_$ok;
    get_pid_result = 0;
    status = 0x12345678;
}

/* 0x00E5CDE6-0x00E5CE44 with start 0x1000, end 0x13FF: size 0x400, buckets
 * 4, bucket_size 4, shift 2, multiplier (0x400 + 4 - 1) >> 2 = 0x100. */
TEST(cmd0_range_arithmetic_and_fields)
{
    int16_t cmd = 0;
    uint32_t range[3] = { 0x1000, 0x13FF, 0 };

    reset();
    PCHIST_$CNTL(&cmd, range, out, &status);

    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(1, stop_calls);
    ASSERT_EQ(&cmd, stop_cmd_ptr);
    ASSERT_EQ(0x1000, PCHIST_$HISTOGRAM.range_start);
    ASSERT_EQ(4, PCHIST_$HISTOGRAM.bucket_size);
    ASSERT_EQ(2, PCHIST_$HISTOGRAM.shift);
    ASSERT_EQ(0x100, PCHIST_$HISTOGRAM.multiplier);
    /* range_end = start + bucket_size * multiplier - 1 */
    ASSERT_EQ(0x1000 + 4 * 0x100 - 1, PCHIST_$HISTOGRAM.range_end);
    ASSERT_EQ(0, PCHIST_$HISTOGRAM.pid_filter);
    ASSERT_EQ(1, PCHIST_$HISTOGRAM.enabled);
    ASSERT_EQ(0, PCHIST_$HISTOGRAM.total_samples);
    ASSERT_EQ(0, PCHIST_$HISTOGRAM.histogram[0]);
    ASSERT_EQ(0, PCHIST_$HISTOGRAM.histogram[255]);
    /* 0x00E5CEEC `st` on the HIGH byte of doalign only */
    ASSERT_EQ(0xFF5A, (uint16_t)PCHIST_$HISTOGRAM.doalign);
    ASSERT_EQ(-1, (int)PCHIST_$CONTROL.histogram_enabled);
    ASSERT_EQ(0, (int)PCHIST_$CONTROL.doalign);
    /* command 0 bumps the system count under the exclusion */
    ASSERT_EQ(1, PCHIST_$CONTROL.sys_profiling_count);
    ASSERT_EQ(1, excl_start_calls);
    ASSERT_EQ(1, excl_stop_calls);
    ASSERT_EQ(1, enable_calls);
    ASSERT_EQ(0, enable_arg);
    /* the wire-area call covers the histogram record */
    ASSERT_EQ(1, wire_calls);
    ASSERT_EQ(ARCH_PTR_TO_VA(&PCHIST_$HISTOGRAM), wire_start);
    ASSERT_EQ(ARCH_PTR_TO_VA(&PCHIST_$HISTOGRAM) + sizeof(PCHIST_$HISTOGRAM), wire_end);
    ASSERT_EQ(&PCHIST_$WIRE_PAGES[1], wire_pages);
    ASSERT_EQ(3, wire_max);
    ASSERT_EQ(&PCHIST_$WIRED_COUNT, wire_count_ret);
    /* the record was copied out (compared field-wise: the copy is a raw
     * longword copy, so the host's byte order is what lands in `out`) */
    ASSERT_EQ(1, ((pchist_histogram_t *)out)->enabled);
    ASSERT_EQ(0xFF5A, (uint16_t)((pchist_histogram_t *)out)->doalign);
}

/* Both range words zero: multiplier 0x100, bucket 0x1000000, shift 0x18. */
TEST(cmd3_zero_range_defaults_and_doalign)
{
    int16_t cmd = 3;
    uint32_t range[3] = { 0, 0, 0 };

    reset();
    PCHIST_$CNTL(&cmd, range, out, &status);

    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(0x100, PCHIST_$HISTOGRAM.multiplier);
    ASSERT_EQ(0x1000000, PCHIST_$HISTOGRAM.bucket_size);
    ASSERT_EQ(0x18, PCHIST_$HISTOGRAM.shift);
    ASSERT_EQ(-1, (int)PCHIST_$CONTROL.doalign);
    /* command 3 does not touch the system count */
    ASSERT_EQ(0, PCHIST_$CONTROL.sys_profiling_count);
    ASSERT_EQ(0, excl_start_calls);
}

/* 0x00E5CE6C `tst.w (0x8,A3)`: the PID is the word at range +8, the HIGH
 * half of range[2]; its low half is ignored. */
TEST(pid_filter_is_the_high_word_of_range_2)
{
    int16_t cmd = 0;
    uint32_t range[3] = { 0x1000, 0x13FF, 0x00070000u | 0xFFFF };

    reset();
    PCHIST_$CNTL(&cmd, range, out, &status);

    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(7, PCHIST_$HISTOGRAM.pid_filter);
    ASSERT_EQ(0, upid_calls);
}

/* A negative word is a UPID: negated, converted, then PROC2_$GET_PID. */
TEST(negative_pid_word_is_a_upid)
{
    int16_t cmd = 0;
    uint32_t range[3] = { 0x1000, 0x13FF, (uint32_t)(uint16_t)-25 << 16 };

    reset();
    get_pid_result = 9;
    PCHIST_$CNTL(&cmd, range, out, &status);

    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(1, upid_calls);
    ASSERT_EQ(25, upid_seen);
    ASSERT_EQ(1, get_pid_calls);
    ASSERT_EQ(0x11111111, get_pid_uid_high);
    ASSERT_EQ(9, PCHIST_$HISTOGRAM.pid_filter);
}

/* 0x00E5CE90 / 0x00E5CEA8: a failing lookup goes to UNWIRE_CLEANUP and
 * leaves the status; nothing else is written. */
TEST(failed_upid_lookup_cleans_up)
{
    int16_t cmd = 0;
    uint32_t range[3] = { 0x1000, 0x13FF, (uint32_t)(uint16_t)-1 << 16 };

    reset();
    upid_status = 0x00190001;
    PCHIST_$CNTL(&cmd, range, out, &status);

    ASSERT_EQ(0x00190001, status);
    ASSERT_EQ(1, unwire_calls);
    ASSERT_EQ(0, get_pid_calls);
    ASSERT_EQ(0, wire_calls);
    ASSERT_EQ(0x5A5A, (uint16_t)PCHIST_$HISTOGRAM.enabled);
}

/* Command 1 stops and copies; command 2 only copies. */
TEST(cmd1_stops_and_copies_cmd2_only_copies)
{
    int16_t cmd = 1;
    uint32_t range[3] = { 0, 0, 0 };

    reset();
    PCHIST_$HISTOGRAM.enabled = 0x1234;
    PCHIST_$CNTL(&cmd, range, out, &status);
    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(1, stop_calls);
    ASSERT_EQ(0, wire_calls);
    ASSERT_EQ(0x1234, ((pchist_histogram_t *)out)->enabled);
    ASSERT_EQ(0x5A5A, (uint16_t)((pchist_histogram_t *)out)->doalign);

    reset();
    cmd = 2;
    PCHIST_$CNTL(&cmd, range, out, &status);
    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(0, stop_calls);
    ASSERT_EQ(0x5A5A, (uint16_t)((pchist_histogram_t *)out)->enabled);
    ASSERT_EQ(0x5A5A, (uint16_t)((pchist_histogram_t *)out)->doalign);
}

/* end < start -> size 0x200: buckets 2, bucket_size 2, shift 1,
 * multiplier (0x200 + 1) >> 1 = 0x100. */
TEST(end_before_start_uses_0x200)
{
    int16_t cmd = 0;
    uint32_t range[3] = { 0x5000, 0x1000, 0 };

    reset();
    PCHIST_$CNTL(&cmd, range, out, &status);

    ASSERT_EQ(2, PCHIST_$HISTOGRAM.bucket_size);
    ASSERT_EQ(1, PCHIST_$HISTOGRAM.shift);
    ASSERT_EQ(0x100, PCHIST_$HISTOGRAM.multiplier);
    ASSERT_EQ(0x5000 + 2 * 0x100 - 1, PCHIST_$HISTOGRAM.range_end);
}

int main(void)
{
    printf("PCHIST_$CNTL tests\n");
    RUN_TEST(cmd0_range_arithmetic_and_fields);
    RUN_TEST(cmd3_zero_range_defaults_and_doalign);
    RUN_TEST(pid_filter_is_the_high_word_of_range_2);
    RUN_TEST(negative_pid_word_is_a_upid);
    RUN_TEST(failed_upid_lookup_cleans_up);
    RUN_TEST(cmd1_stops_and_copies_cmd2_only_copies);
    RUN_TEST(end_before_start_uses_0x200);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
