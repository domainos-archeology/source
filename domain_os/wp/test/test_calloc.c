/*
 * wp/test/test_calloc.c - Unit tests for WP_$CALLOC
 *
 * Tests that WP_$CALLOC correctly calls ML_$LOCK, ast_$allocate_pages,
 * and ML_$UNLOCK in the proper order, with correct arguments, and
 * copies the result to the output pointer.
 */

#include <stdio.h>
#include <assert.h>
#include <stdint.h>
#include <string.h>

/* Minimal type stubs */
typedef long status_$t;
#define status_$ok 0

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

/*
 * Call tracking for mocks
 */
#define MAX_CALLS 16

typedef enum {
    CALL_ML_LOCK,
    CALL_AST_ALLOCATE_PAGES,
    CALL_ML_UNLOCK
} call_type_t;

typedef struct {
    call_type_t type;
    uint32_t arg1;
    uint32_t arg2;
} call_record_t;

static call_record_t call_log[MAX_CALLS];
static int call_count = 0;

/* Mock state: what ast_$allocate_pages should write to ppn_array[0] */
static uint32_t mock_ppn_result = 0;

static void reset_call_log(void)
{
    call_count = 0;
    mock_ppn_result = 0;
    memset(call_log, 0, sizeof(call_log));
}

/*
 * Mock implementations
 */
void ML_$LOCK(int16_t resource_id)
{
    if (call_count < MAX_CALLS) {
        call_log[call_count].type = CALL_ML_LOCK;
        call_log[call_count].arg1 = (uint32_t)resource_id;
        call_count++;
    }
}

void ML_$UNLOCK(int16_t resource_id)
{
    if (call_count < MAX_CALLS) {
        call_log[call_count].type = CALL_ML_UNLOCK;
        call_log[call_count].arg1 = (uint32_t)resource_id;
        call_count++;
    }
}

int16_t ast_$allocate_pages(uint32_t count_flags, uint32_t *ppn_array)
{
    if (call_count < MAX_CALLS) {
        call_log[call_count].type = CALL_AST_ALLOCATE_PAGES;
        call_log[call_count].arg1 = count_flags;
        call_count++;
    }
    /* Simulate allocating one page */
    ppn_array[0] = mock_ppn_result;
    return 1;
}

/*
 * WP_$CALLOC - function under test (reimplemented with mocks)
 */
#define WP_LOCK_ID 0x14

void WP_$CALLOC(uint32_t *ppn_out, status_$t *status)
{
    uint32_t ppn_buf[32];

    ML_$LOCK(WP_LOCK_ID);
    ast_$allocate_pages(0x00010001, ppn_buf);
    ML_$UNLOCK(WP_LOCK_ID);

    *ppn_out = ppn_buf[0];
    *status = status_$ok;
}

/*
 * Tests
 */

TEST(calls_in_correct_order)
{
    uint32_t ppn = 0;
    status_$t status = -1;
    reset_call_log();
    mock_ppn_result = 0x42;

    WP_$CALLOC(&ppn, &status);

    ASSERT_EQ(3, call_count);

    /* First: ML_$LOCK(0x14) */
    ASSERT_EQ(CALL_ML_LOCK, call_log[0].type);
    ASSERT_EQ(0x14, call_log[0].arg1);

    /* Second: ast_$allocate_pages(0x00010001, ...) */
    ASSERT_EQ(CALL_AST_ALLOCATE_PAGES, call_log[1].type);
    ASSERT_EQ(0x00010001, call_log[1].arg1);

    /* Third: ML_$UNLOCK(0x14) */
    ASSERT_EQ(CALL_ML_UNLOCK, call_log[2].type);
    ASSERT_EQ(0x14, call_log[2].arg1);
}

TEST(copies_ppn_to_output)
{
    uint32_t ppn = 0;
    status_$t status = -1;
    reset_call_log();
    mock_ppn_result = 0xDEAD;

    WP_$CALLOC(&ppn, &status);

    ASSERT_EQ(0xDEAD, ppn);
}

TEST(sets_status_ok)
{
    uint32_t ppn = 0;
    status_$t status = -1;
    reset_call_log();
    mock_ppn_result = 0x1;

    WP_$CALLOC(&ppn, &status);

    ASSERT_EQ(status_$ok, status);
}

TEST(count_flags_is_0x00010001)
{
    uint32_t ppn = 0;
    status_$t status = -1;
    reset_call_log();

    WP_$CALLOC(&ppn, &status);

    /* Verify count_flags: low word = 1 (request), high word = 1 (minimum) */
    ASSERT_EQ(0x00010001, call_log[1].arg1);
}

int main(void)
{
    printf("test_calloc:\n");

    RUN_TEST(calls_in_correct_order);
    RUN_TEST(copies_ppn_to_output);
    RUN_TEST(sets_status_ok);
    RUN_TEST(count_flags_is_0x00010001);

    printf("\n  Results: %d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
