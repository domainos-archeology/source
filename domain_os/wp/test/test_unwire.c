/*
 * wp/test/test_unwire.c - Unit tests for WP_$UNWIRE
 *
 * Tests that WP_$UNWIRE correctly calls ML_$LOCK, MMAP_$UNWIRE,
 * and ML_$UNLOCK in the proper order.
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
    CALL_MMAP_UNWIRE,
    CALL_ML_UNLOCK
} call_type_t;

typedef struct {
    call_type_t type;
    int32_t arg;
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
        call_log[call_count].arg = resource_id;
        call_count++;
    }
}

void ML_$UNLOCK(int16_t resource_id)
{
    if (call_count < MAX_CALLS) {
        call_log[call_count].type = CALL_ML_UNLOCK;
        call_log[call_count].arg = resource_id;
        call_count++;
    }
}

void MMAP_$UNWIRE(uint32_t vpn)
{
    if (call_count < MAX_CALLS) {
        call_log[call_count].type = CALL_MMAP_UNWIRE;
        call_log[call_count].arg = (int32_t)vpn;
        call_count++;
    }
}

/*
 * WP_$UNWIRE - function under test (reimplemented with mocks)
 */
#define WP_LOCK_ID 0x14

void WP_$UNWIRE(uint32_t wired_addr)
{
    ML_$LOCK(WP_LOCK_ID);
    MMAP_$UNWIRE(wired_addr);
    ML_$UNLOCK(WP_LOCK_ID);
}

/*
 * Tests
 */

TEST(calls_in_correct_order)
{
    reset_call_log();

    WP_$UNWIRE(0x1000);

    ASSERT_EQ(3, call_count);

    /* First: ML_$LOCK(0x14) */
    ASSERT_EQ(CALL_ML_LOCK, call_log[0].type);
    ASSERT_EQ(0x14, call_log[0].arg);

    /* Second: MMAP_$UNWIRE(0x1000) */
    ASSERT_EQ(CALL_MMAP_UNWIRE, call_log[1].type);
    ASSERT_EQ(0x1000, call_log[1].arg);

    /* Third: ML_$UNLOCK(0x14) */
    ASSERT_EQ(CALL_ML_UNLOCK, call_log[2].type);
    ASSERT_EQ(0x14, call_log[2].arg);
}

TEST(passes_address_correctly)
{
    reset_call_log();

    WP_$UNWIRE(0xDEADBEEF);

    ASSERT_EQ(3, call_count);
    ASSERT_EQ((int32_t)0xDEADBEEF, call_log[1].arg);
}

TEST(zero_address)
{
    reset_call_log();

    WP_$UNWIRE(0);

    ASSERT_EQ(3, call_count);
    ASSERT_EQ(CALL_ML_LOCK, call_log[0].type);
    ASSERT_EQ(CALL_MMAP_UNWIRE, call_log[1].type);
    ASSERT_EQ(0, call_log[1].arg);
    ASSERT_EQ(CALL_ML_UNLOCK, call_log[2].type);
}

int main(void)
{
    printf("test_unwire:\n");

    RUN_TEST(calls_in_correct_order);
    RUN_TEST(passes_address_correctly);
    RUN_TEST(zero_address);

    printf("\n  Results: %d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
