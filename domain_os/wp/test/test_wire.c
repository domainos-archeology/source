/*
 * wp/test/test_wire.c - Unit tests for WP_$WIRE (0x00E071B0)
 *
 * The real wp/wire.c is #included below, so the function under test is the
 * one that gets built into the kernel; ML_$LOCK, ML_$UNLOCK and MMAP_$WIRE
 * are stubbed here and record what they were handed.
 *
 * Covered:
 *   - the three calls happen, in the order 0x00E071C2 / 0x00E071CE /
 *     0x00E071DC
 *   - both lock calls use resource id 0x14
 *   - the page number reaches MMAP_$WIRE unchanged, including the full
 *     32-bit range
 */

#include <stdio.h>
#include <string.h>

#include "wp/wp_internal.h"
#include "mmap/mmap.h"

/* ============================================================================
 * Test framework
 * ============================================================================ */

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    printf("  Running %s... ", #name); \
    test_##name(); \
    tests_passed++; \
    printf("PASSED\n"); \
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

/* ============================================================================
 * Mocks
 * ============================================================================ */

#define MAX_CALLS 16

typedef enum {
    CALL_ML_LOCK,
    CALL_MMAP_WIRE,
    CALL_ML_UNLOCK
} call_type_t;

typedef struct {
    call_type_t type;
    uint32_t    arg;
} call_record_t;

static call_record_t call_log[MAX_CALLS];
static int call_count;

static void record(call_type_t type, uint32_t arg)
{
    if (call_count < MAX_CALLS) {
        call_log[call_count].type = type;
        call_log[call_count].arg = arg;
    }
    call_count++;
}

static void reset_call_log(void)
{
    call_count = 0;
    memset(call_log, 0, sizeof(call_log));
}

void ML_$LOCK(int16_t resource_id)   { record(CALL_ML_LOCK, (uint32_t)(uint16_t)resource_id); }
void ML_$UNLOCK(int16_t resource_id) { record(CALL_ML_UNLOCK, (uint32_t)(uint16_t)resource_id); }
void MMAP_$WIRE(uint32_t vpn)        { record(CALL_MMAP_WIRE, vpn); }

/* ============================================================================
 * Code under test
 * ============================================================================ */

#include "../wire.c"

/* ============================================================================
 * Tests
 * ============================================================================ */

TEST(calls_in_order_with_the_wp_lock)
{
    reset_call_log();

    WP_$WIRE(0x1000);

    ASSERT_EQ(3, call_count);

    ASSERT_EQ(CALL_ML_LOCK, call_log[0].type);
    ASSERT_EQ(0x14, call_log[0].arg);

    ASSERT_EQ(CALL_MMAP_WIRE, call_log[1].type);
    ASSERT_EQ(0x1000, call_log[1].arg);

    ASSERT_EQ(CALL_ML_UNLOCK, call_log[2].type);
    ASSERT_EQ(0x14, call_log[2].arg);
}

TEST(page_number_passed_by_value_unchanged)
{
    reset_call_log();

    WP_$WIRE(0xDEADBEEFu);

    ASSERT_EQ(3, call_count);
    ASSERT_EQ(0xDEADBEEFu, call_log[1].arg);
}

TEST(zero_page_still_takes_the_lock)
{
    reset_call_log();

    WP_$WIRE(0);

    ASSERT_EQ(3, call_count);
    ASSERT_EQ(CALL_ML_LOCK, call_log[0].type);
    ASSERT_EQ(CALL_MMAP_WIRE, call_log[1].type);
    ASSERT_EQ(0u, call_log[1].arg);
    ASSERT_EQ(CALL_ML_UNLOCK, call_log[2].type);
}

int main(void)
{
    printf("test_wire:\n");

    RUN_TEST(calls_in_order_with_the_wp_lock);
    RUN_TEST(page_number_passed_by_value_unchanged);
    RUN_TEST(zero_page_still_takes_the_lock);

    printf("\n  Results: %d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
