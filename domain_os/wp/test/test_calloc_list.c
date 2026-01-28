/*
 * wp/test/test_calloc_list.c - Unit tests for WP_$CALLOC_LIST
 *
 * Tests that WP_$CALLOC_LIST correctly calls ML_$LOCK, ast_$allocate_pages,
 * and ML_$UNLOCK in the proper order, with correct count_flags packing.
 */

#include <stdio.h>
#include <assert.h>
#include <stdint.h>
#include <string.h>

/* Minimal type stubs */
typedef long status_$t;

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
} call_record_t;

static call_record_t call_log[MAX_CALLS];
static int call_count = 0;

static void reset_call_log(void)
{
    call_count = 0;
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
    return (int16_t)(count_flags & 0xFFFF);
}

/*
 * WP_$CALLOC_LIST - function under test (reimplemented with mocks)
 */
#define WP_LOCK_ID 0x14

void WP_$CALLOC_LIST(int16_t count, uint32_t *ppn_arr)
{
    uint32_t count_flags;

    count_flags = ((uint32_t)(uint16_t)count << 16) | (uint16_t)count;

    ML_$LOCK(WP_LOCK_ID);
    ast_$allocate_pages(count_flags, ppn_arr);
    ML_$UNLOCK(WP_LOCK_ID);
}

/*
 * Tests
 */

TEST(calls_in_correct_order)
{
    uint32_t ppn_arr[4];
    reset_call_log();

    WP_$CALLOC_LIST(4, ppn_arr);

    ASSERT_EQ(3, call_count);

    /* First: ML_$LOCK(0x14) */
    ASSERT_EQ(CALL_ML_LOCK, call_log[0].type);
    ASSERT_EQ(0x14, call_log[0].arg1);

    /* Second: ast_$allocate_pages */
    ASSERT_EQ(CALL_AST_ALLOCATE_PAGES, call_log[1].type);

    /* Third: ML_$UNLOCK(0x14) */
    ASSERT_EQ(CALL_ML_UNLOCK, call_log[2].type);
    ASSERT_EQ(0x14, call_log[2].arg1);
}

TEST(count_flags_packing_count_1)
{
    uint32_t ppn_arr[1];
    reset_call_log();

    WP_$CALLOC_LIST(1, ppn_arr);

    /* count=1 -> count_flags = 0x00010001 */
    ASSERT_EQ(0x00010001, call_log[1].arg1);
}

TEST(count_flags_packing_count_4)
{
    uint32_t ppn_arr[4];
    reset_call_log();

    WP_$CALLOC_LIST(4, ppn_arr);

    /* count=4 -> count_flags = 0x00040004 */
    ASSERT_EQ(0x00040004, call_log[1].arg1);
}

TEST(count_flags_packing_count_32)
{
    uint32_t ppn_arr[32];
    reset_call_log();

    WP_$CALLOC_LIST(32, ppn_arr);

    /* count=32 -> count_flags = 0x00200020 */
    ASSERT_EQ(0x00200020, call_log[1].arg1);
}

TEST(count_flags_packing_count_256)
{
    uint32_t ppn_arr[256];
    reset_call_log();

    WP_$CALLOC_LIST(256, ppn_arr);

    /* count=256 (0x100) -> count_flags = 0x01000100 */
    ASSERT_EQ(0x01000100, call_log[1].arg1);
}

int main(void)
{
    printf("test_calloc_list:\n");

    RUN_TEST(calls_in_correct_order);
    RUN_TEST(count_flags_packing_count_1);
    RUN_TEST(count_flags_packing_count_4);
    RUN_TEST(count_flags_packing_count_32);
    RUN_TEST(count_flags_packing_count_256);

    printf("\n  Results: %d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
