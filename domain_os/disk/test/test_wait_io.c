/*
 * disk/test/test_wait_io.c - Unit tests for disk_$wait_io
 *
 * Tests the disk I/O wait function by mocking EC_$WAIT, DISK_$ERROR_QUE,
 * DISK_$DATA, PROC1_$CURRENT, and TIME_$CLOCKH. Verifies:
 *   - Immediate I/O completion (EC_$WAIT returns 0 on first call)
 *   - Error detection increments error_wait_val
 *   - Timeout polling does not increment error_wait_val
 *   - Disk mask filtering (only checked disks call ERROR_QUE)
 *   - Multiple errors across multiple disks
 *   - Multiple wait iterations before completion
 */

#include <stdio.h>
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

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

#define ASSERT_TRUE(cond) do { \
    if (!(cond)) { \
        printf("FAILED\n    Condition false at line %d\n", __LINE__); \
        tests_failed++; \
        return; \
    } \
} while(0)

/* ================================================================
 * Mock infrastructure
 * ================================================================ */

/* Prevent inclusion of kernel headers */
#define BASE_H
#define EC_H
#define ML_H
#define PROC1_H
#define PROC1_CONFIG_H
#define PROC2_H
#define DBUF_H
#define TIME_H
#define DISK_H
#define DISK_INTERNAL_H

/* Types needed */
typedef uint32_t status_$t;
typedef struct { uint32_t high; uint32_t low; } apollo_uid_t;
#define uid_t apollo_uid_t

/* ec_$eventcount_t mock */
typedef struct ec_$eventcount_t {
    union {
        int32_t value;
        int32_t count;
    };
    void *waiter_list_head;
    void *waiter_list_tail;
} ec_$eventcount_t;

/* EC_$WAIT argument records: two 3-element arrays passed BY VALUE
 * (0x00E20610); mirrors ec/ec.h, which this test does not include. */
typedef struct ec_$wait_ecs_t {
    ec_$eventcount_t *ec[3];
} ec_$wait_ecs_t;

typedef struct ec_$wait_vals_t {
    int32_t val[3];
} ec_$wait_vals_t;

/* ML exclusion mock */
typedef struct { int32_t f1; int32_t f2; } ml_$exclusion_t;

/* DISK subsystem constants */
#define DISK_VOLUME_SIZE    0x48

/* DMOD per-process constants */
#define DMOD_PER_PROC_SIZE    0x1c
#define DMOD_PER_PROC_IO_EC   0x378
#define DMOD_PER_PROC_ERR_EC  0x384
#define DMOD_VOL_ERROR_QUE    0x7c
#define DMOD_NUM_VOLUMES      10
#define DMOD_WAIT_TIMEOUT     0xf0

/* Module data - large enough for per-process ECs + volume descriptors */
#define MOCK_DATA_SIZE  0x1000
static uint8_t mock_disk_data[MOCK_DATA_SIZE];
uint8_t *DISK_$DATA = mock_disk_data;

/* PROC1_$CURRENT mock */
uint16_t PROC1_$CURRENT = 1;

/* TIME_$CLOCKH mock */
uint32_t TIME_$CLOCKH = 1000;

/* ================================================================
 * EC_$WAIT mock
 * ================================================================ */

/* Control what EC_$WAIT returns on each call */
#define MAX_WAIT_CALLS 16
static int16_t ec_wait_returns[MAX_WAIT_CALLS];
static int ec_wait_call_count = 0;
static int ec_wait_max_calls = 0;

/* Record what EC_$WAIT was called with */
static ec_$eventcount_t *last_ecs[3];
static int32_t last_wait_vals[3];

/* Both 3-element arrays arrive BY VALUE (0xE20610); see ec/ec.h. */
int16_t EC_$WAIT(ec_$wait_ecs_t ecs, ec_$wait_vals_t vals)
{
    /* Record args */
    last_ecs[0] = ecs.ec[0];
    last_ecs[1] = ecs.ec[1];
    last_ecs[2] = ecs.ec[2];
    last_wait_vals[0] = vals.val[0];
    last_wait_vals[1] = vals.val[1];
    last_wait_vals[2] = vals.val[2];

    if (ec_wait_call_count < ec_wait_max_calls) {
        return ec_wait_returns[ec_wait_call_count++];
    }
    /* Default: I/O complete */
    ec_wait_call_count++;
    return 0;
}

/* ================================================================
 * DISK_$ERROR_QUE mock
 * ================================================================ */

#define MAX_ERROR_QUE_CALLS 32
static int error_que_call_count = 0;

/* Track calls */
static void *error_que_req_args[MAX_ERROR_QUE_CALLS];
static uint16_t error_que_flag_args[MAX_ERROR_QUE_CALLS];

/* Control what error result byte 0 is set to for each call */
static uint8_t error_que_result_byte0[MAX_ERROR_QUE_CALLS];

int16_t DISK_$ERROR_QUE(void *req, uint16_t param_2, int8_t *param_3)
{
    if (error_que_call_count < MAX_ERROR_QUE_CALLS) {
        error_que_req_args[error_que_call_count] = req;
        error_que_flag_args[error_que_call_count] = param_2;

        /* Write result byte 0 */
        ((uint8_t *)param_3)[0] = error_que_result_byte0[error_que_call_count];

        error_que_call_count++;
    }
}

/* ================================================================
 * Test helpers
 * ================================================================ */

static void reset_mocks(void)
{
    memset(mock_disk_data, 0, sizeof(mock_disk_data));
    ec_wait_call_count = 0;
    ec_wait_max_calls = 0;
    error_que_call_count = 0;
    memset(ec_wait_returns, 0, sizeof(ec_wait_returns));
    memset(last_ecs, 0, sizeof(last_ecs));
    memset(last_wait_vals, 0, sizeof(last_wait_vals));
    memset(error_que_req_args, 0, sizeof(error_que_req_args));
    memset(error_que_flag_args, 0, sizeof(error_que_flag_args));
    memset(error_que_result_byte0, 0, sizeof(error_que_result_byte0));
    PROC1_$CURRENT = 1;
    TIME_$CLOCKH = 1000;
}

/* Include the source file under test */
#include "../wait_io.c"

/* ================================================================
 * Tests
 * ================================================================ */

/*
 * Test: Immediate I/O completion
 * EC_$WAIT returns 0 on first call -> function returns immediately.
 * No DISK_$ERROR_QUE calls should be made.
 */
TEST(immediate_completion)
{
    reset_mocks();
    ec_wait_returns[0] = 0;  /* I/O complete */
    ec_wait_max_calls = 1;

    int32_t io_val = 100;
    int32_t err_val = 50;

    disk_$wait_io(0x3FE, &io_val, &err_val);  /* bits 1-9 set */

    ASSERT_EQ(1, ec_wait_call_count);
    ASSERT_EQ(0, error_que_call_count);
    ASSERT_EQ(100, io_val);   /* unchanged */
    ASSERT_EQ(50, err_val);   /* unchanged */
}

/*
 * Test: EC_$WAIT is called with correct EC pointers
 */
TEST(ec_pointers_correct)
{
    reset_mocks();
    ec_wait_returns[0] = 0;
    ec_wait_max_calls = 1;
    PROC1_$CURRENT = 2;

    int32_t io_val = 10;
    int32_t err_val = 20;

    disk_$wait_io(0x002, &io_val, &err_val);

    /* Per-process base = data + PID * 0x1c = data + 2 * 28 = data + 56 */
    uint8_t *expected_per_proc = mock_disk_data + (int16_t)(2 * DMOD_PER_PROC_SIZE);

    ASSERT_EQ((uintptr_t)(expected_per_proc + DMOD_PER_PROC_IO_EC),
              (uintptr_t)last_ecs[0]);
    ASSERT_EQ((uintptr_t)(expected_per_proc + DMOD_PER_PROC_ERR_EC),
              (uintptr_t)last_ecs[1]);
    ASSERT_EQ((uintptr_t)&TIME_$CLOCKH, (uintptr_t)last_ecs[2]);
}

/*
 * Test: EC_$WAIT wait values are correct
 */
TEST(wait_values_correct)
{
    reset_mocks();
    ec_wait_returns[0] = 0;
    ec_wait_max_calls = 1;
    TIME_$CLOCKH = 5000;

    int32_t io_val = 42;
    int32_t err_val = 99;

    disk_$wait_io(0x002, &io_val, &err_val);

    ASSERT_EQ(42, last_wait_vals[0]);   /* io_wait_val */
    ASSERT_EQ(99, last_wait_vals[1]);   /* error_wait_val */
    ASSERT_EQ((int32_t)(5000 + DMOD_WAIT_TIMEOUT), last_wait_vals[2]);  /* timeout */
}

/*
 * Test: Error detected on one disk increments error_wait_val
 * EC_$WAIT returns 1 (error EC) then 0 (complete).
 * Disk 1 (bit 1) has an error (result byte 0 = 0x80).
 */
TEST(error_increments_wait_val)
{
    reset_mocks();
    ec_wait_returns[0] = 1;  /* error EC fired */
    ec_wait_returns[1] = 0;  /* I/O complete */
    ec_wait_max_calls = 2;

    /* Only disk 1 in mask */
    uint16_t mask = 0x002;  /* bit 1 */

    /* Disk 1 reports an error (bit 7 set) */
    error_que_result_byte0[0] = 0x80;

    int32_t io_val = 100;
    int32_t err_val = 50;

    disk_$wait_io(mask, &io_val, &err_val);

    ASSERT_EQ(2, ec_wait_call_count);
    ASSERT_EQ(1, error_que_call_count);
    ASSERT_EQ(100, io_val);   /* unchanged */
    ASSERT_EQ(51, err_val);   /* incremented by 1 */

    /* Verify ERROR_QUE was called with is_timeout=0 (error, not timeout) */
    ASSERT_EQ(0, error_que_flag_args[0]);
}

/*
 * Test: Timeout does NOT increment error_wait_val even with error result
 * EC_$WAIT returns 2 (timeout) then 0 (complete).
 */
TEST(timeout_no_increment)
{
    reset_mocks();
    ec_wait_returns[0] = 2;  /* timeout */
    ec_wait_returns[1] = 0;  /* I/O complete */
    ec_wait_max_calls = 2;

    uint16_t mask = 0x002;  /* bit 1 */

    /* Disk 1 reports error-like result, but it's a timeout poll */
    error_que_result_byte0[0] = 0xFF;

    int32_t io_val = 100;
    int32_t err_val = 50;

    disk_$wait_io(mask, &io_val, &err_val);

    ASSERT_EQ(2, ec_wait_call_count);
    ASSERT_EQ(1, error_que_call_count);
    ASSERT_EQ(50, err_val);  /* NOT incremented because it was a timeout */

    /* Verify ERROR_QUE was called with is_timeout=1 */
    ASSERT_EQ(1, error_que_flag_args[0]);
}

/*
 * Test: Disk mask filtering - only checked disks call ERROR_QUE
 * Mask = 0x00A (bits 1 and 3 set), so only disks 1 and 3 are checked.
 */
TEST(mask_filtering)
{
    reset_mocks();
    ec_wait_returns[0] = 1;  /* error */
    ec_wait_returns[1] = 0;  /* complete */
    ec_wait_max_calls = 2;

    uint16_t mask = 0x00A;  /* bits 1 and 3 */

    /* Both disks have no error */
    error_que_result_byte0[0] = 0x00;
    error_que_result_byte0[1] = 0x00;

    int32_t io_val = 100;
    int32_t err_val = 50;

    disk_$wait_io(mask, &io_val, &err_val);

    /* Only 2 calls to ERROR_QUE (disks 1 and 3) */
    ASSERT_EQ(2, error_que_call_count);
    ASSERT_EQ(50, err_val);  /* no errors detected */

    /* Verify the correct disk descriptors were passed */
    /* Disk 1: data + 1*0x48 + 0x7c = data + 0xC4 */
    ASSERT_EQ((uintptr_t)(mock_disk_data + 1 * DISK_VOLUME_SIZE + DMOD_VOL_ERROR_QUE),
              (uintptr_t)error_que_req_args[0]);
    /* Disk 3: data + 3*0x48 + 0x7c = data + 0x154 */
    ASSERT_EQ((uintptr_t)(mock_disk_data + 3 * DISK_VOLUME_SIZE + DMOD_VOL_ERROR_QUE),
              (uintptr_t)error_que_req_args[1]);
}

/*
 * Test: Multiple errors across multiple disks
 * Disks 1, 2, 3 all have errors on error EC fire.
 */
TEST(multiple_disk_errors)
{
    reset_mocks();
    ec_wait_returns[0] = 1;  /* error */
    ec_wait_returns[1] = 0;  /* complete */
    ec_wait_max_calls = 2;

    uint16_t mask = 0x00E;  /* bits 1, 2, 3 */

    /* All 3 disks report errors */
    error_que_result_byte0[0] = 0x80;  /* disk 1 */
    error_que_result_byte0[1] = 0xC0;  /* disk 2 */
    error_que_result_byte0[2] = 0xFF;  /* disk 3 */

    int32_t io_val = 100;
    int32_t err_val = 50;

    disk_$wait_io(mask, &io_val, &err_val);

    ASSERT_EQ(3, error_que_call_count);
    ASSERT_EQ(53, err_val);  /* incremented 3 times */
}

/*
 * Test: No error when result byte 0 is positive (bit 7 clear)
 */
TEST(no_error_positive_result)
{
    reset_mocks();
    ec_wait_returns[0] = 1;  /* error EC */
    ec_wait_returns[1] = 0;  /* complete */
    ec_wait_max_calls = 2;

    uint16_t mask = 0x002;  /* bit 1 */
    error_que_result_byte0[0] = 0x7F;  /* bit 7 clear - no error */

    int32_t io_val = 100;
    int32_t err_val = 50;

    disk_$wait_io(mask, &io_val, &err_val);

    ASSERT_EQ(1, error_que_call_count);
    ASSERT_EQ(50, err_val);  /* NOT incremented */
}

/*
 * Test: Multiple wait iterations before completion
 * Two error rounds, then completion.
 */
TEST(multiple_iterations)
{
    reset_mocks();
    ec_wait_returns[0] = 1;  /* error */
    ec_wait_returns[1] = 2;  /* timeout */
    ec_wait_returns[2] = 1;  /* error */
    ec_wait_returns[3] = 0;  /* complete */
    ec_wait_max_calls = 4;

    uint16_t mask = 0x002;  /* bit 1 only */

    /* Round 1 (error): disk 1 has error */
    error_que_result_byte0[0] = 0x80;
    /* Round 2 (timeout): disk 1 reports error-like, but timeout */
    error_que_result_byte0[1] = 0x80;
    /* Round 3 (error): disk 1 has error */
    error_que_result_byte0[2] = 0x80;

    int32_t io_val = 100;
    int32_t err_val = 50;

    disk_$wait_io(mask, &io_val, &err_val);

    ASSERT_EQ(4, ec_wait_call_count);
    ASSERT_EQ(3, error_que_call_count);
    /* Only rounds 1 and 3 increment (round 2 is timeout) */
    ASSERT_EQ(52, err_val);
}

/*
 * Test: All 10 disks checked with full mask
 */
TEST(all_disks_checked)
{
    reset_mocks();
    ec_wait_returns[0] = 1;  /* error */
    ec_wait_returns[1] = 0;  /* complete */
    ec_wait_max_calls = 2;

    uint16_t mask = 0x7FE;  /* bits 1-10 all set */

    /* No errors on any disk */
    memset(error_que_result_byte0, 0x00, sizeof(error_que_result_byte0));

    int32_t io_val = 100;
    int32_t err_val = 50;

    disk_$wait_io(mask, &io_val, &err_val);

    /* All 10 disks should be checked */
    ASSERT_EQ(10, error_que_call_count);
    ASSERT_EQ(50, err_val);  /* no errors */

    /* Verify disk descriptors: disk N at data + N*0x48 + 0x7c */
    for (int i = 0; i < 10; i++) {
        uintptr_t expected = (uintptr_t)(mock_disk_data + (i + 1) * DISK_VOLUME_SIZE + DMOD_VOL_ERROR_QUE);
        ASSERT_EQ(expected, (uintptr_t)error_que_req_args[i]);
    }
}

/*
 * Test: Empty mask means no ERROR_QUE calls
 * Only bit 0 would be checked, but bits 1-10 are the valid range.
 */
TEST(empty_mask_no_calls)
{
    reset_mocks();
    ec_wait_returns[0] = 1;  /* error */
    ec_wait_returns[1] = 0;  /* complete */
    ec_wait_max_calls = 2;

    uint16_t mask = 0x000;  /* no bits set */

    int32_t io_val = 100;
    int32_t err_val = 50;

    disk_$wait_io(mask, &io_val, &err_val);

    ASSERT_EQ(0, error_que_call_count);
    ASSERT_EQ(50, err_val);
}

/* ================================================================
 * Main
 * ================================================================ */

int main(void)
{
    printf("disk_$wait_io tests:\n");

    RUN_TEST(immediate_completion);
    RUN_TEST(ec_pointers_correct);
    RUN_TEST(wait_values_correct);
    RUN_TEST(error_increments_wait_val);
    RUN_TEST(timeout_no_increment);
    RUN_TEST(mask_filtering);
    RUN_TEST(multiple_disk_errors);
    RUN_TEST(no_error_positive_result);
    RUN_TEST(multiple_iterations);
    RUN_TEST(all_disks_checked);
    RUN_TEST(empty_mask_no_calls);

    printf("\nResults: %d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
