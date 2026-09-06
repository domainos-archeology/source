/*
 * cal/test/test_set_drift.c - Unit tests for CAL_$SET_DRIFT
 *
 * Self-contained host program: includes the implementation under test
 * directly (see cal/test/cal_test.h for the framework).
 */

#include "cal_test.h"

/* Implementation(s) under test */
#include "../cal_data.c"
#include "../set_drift.c"


// CAL_$SET_DRIFT copies a clock_t value to CAL_$TIMEZONE.drift

// Test: Set drift to zero
void test_set_drift_zero(void) {
    // Pre-set drift to non-zero
    CAL_$TIMEZONE.drift.high = 0xFFFFFFFF;
    CAL_$TIMEZONE.drift.low = 0xFFFF;

    clock_t drift = { .high = 0, .low = 0 };
    CAL_$SET_DRIFT(&drift);

    ASSERT_EQ(CAL_$TIMEZONE.drift.high, 0);
    ASSERT_EQ(CAL_$TIMEZONE.drift.low, 0);
}

// Test: Set drift to positive value
void test_set_drift_positive(void) {
    clock_t drift = { .high = 0x12345678, .low = 0xABCD };
    CAL_$SET_DRIFT(&drift);

    ASSERT_EQ(CAL_$TIMEZONE.drift.high, 0x12345678);
    ASSERT_EQ(CAL_$TIMEZONE.drift.low, 0xABCD);
}

// Test: Set drift to small correction (1 second)
void test_set_drift_one_second(void) {
    // 1 second = 250,000 ticks = 0x0003D090
    clock_t drift = { .high = 0x0003, .low = 0xD090 };
    CAL_$SET_DRIFT(&drift);

    ASSERT_EQ(CAL_$TIMEZONE.drift.high, 0x0003);
    ASSERT_EQ(CAL_$TIMEZONE.drift.low, 0xD090);
}

// Test: Set drift overwrites previous value
void test_set_drift_overwrites(void) {
    clock_t drift1 = { .high = 0x1111, .low = 0x2222 };
    CAL_$SET_DRIFT(&drift1);

    ASSERT_EQ(CAL_$TIMEZONE.drift.high, 0x1111);
    ASSERT_EQ(CAL_$TIMEZONE.drift.low, 0x2222);

    clock_t drift2 = { .high = 0x3333, .low = 0x4444 };
    CAL_$SET_DRIFT(&drift2);

    ASSERT_EQ(CAL_$TIMEZONE.drift.high, 0x3333);
    ASSERT_EQ(CAL_$TIMEZONE.drift.low, 0x4444);
}

// Test: Drift source is not modified
void test_set_drift_source_unchanged(void) {
    clock_t drift = { .high = 0xAAAA, .low = 0xBBBB };
    clock_t original = drift;

    CAL_$SET_DRIFT(&drift);

    // Source should be unchanged
    ASSERT_EQ(drift.high, original.high);
    ASSERT_EQ(drift.low, original.low);
}

// Test: Maximum drift value
void test_set_drift_max_value(void) {
    clock_t drift = { .high = 0xFFFFFFFF, .low = 0xFFFF };
    CAL_$SET_DRIFT(&drift);

    ASSERT_EQ(CAL_$TIMEZONE.drift.high, 0xFFFFFFFF);
    ASSERT_EQ(CAL_$TIMEZONE.drift.low, 0xFFFF);
}

int main(void) {
    printf("CAL_$SET_DRIFT tests\n");
    RUN_TEST(set_drift_zero);
    RUN_TEST(set_drift_positive);
    RUN_TEST(set_drift_one_second);
    RUN_TEST(set_drift_overwrites);
    RUN_TEST(set_drift_source_unchanged);
    RUN_TEST(set_drift_max_value);
    TEST_SUMMARY();
    return tests_failed > 0 ? 1 : 0;
}
