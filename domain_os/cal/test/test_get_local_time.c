#include "cal.h"

// These tests require mocking the global state:
// - CAL_$TIMEZONE.utc_delta (timezone offset in minutes)
// - CAL_$TIMEZONE.drift (drift correction)
// - TIME_$CLOCK() system call
//
// The test framework should provide mechanisms to set these values
// before running each test.

// Test: UTC timezone (offset = 0, drift = 0)
// Result should equal system clock
void test_get_local_time_utc(void) {
    // Setup: UTC timezone
    CAL_$TIMEZONE.utc_delta = 0;
    CAL_$TIMEZONE.drift.high = 0;
    CAL_$TIMEZONE.drift.low = 0;

    // Mock TIME_$CLOCK to return a known value
    // (framework should handle this)
    clock_t expected = { .high = 0x12345678, .low = 0xABCD };
    MOCK_TIME_CLOCK(expected);

    clock_t result;
    CAL_$GET_LOCAL_TIME(&result);

    ASSERT_EQ(result.high, expected.high);
    ASSERT_EQ(result.low, expected.low);
}

// Test: Positive timezone offset (EST = UTC-5 = -300 minutes)
// Local time should be 5 hours behind UTC
void test_get_local_time_est(void) {
    // Setup: EST timezone (UTC-5)
    CAL_$TIMEZONE.utc_delta = -300;  // -5 hours in minutes
    CAL_$TIMEZONE.drift.high = 0;
    CAL_$TIMEZONE.drift.low = 0;

    // Mock system clock at noon UTC
    // 12:00:00 = 12 * 3600 = 43200 seconds = 10,800,000,000 ticks
    clock_t utc_noon;
    uint sec = 43200;
    CAL_$SEC_TO_CLOCK(&sec, &utc_noon);
    MOCK_TIME_CLOCK(utc_noon);

    clock_t result;
    CAL_$GET_LOCAL_TIME(&result);

    // Expected: 7:00 AM local (5 hours behind)
    // -300 minutes * 60 = -18000 seconds offset
    // This is SUBTRACTED because utc_delta is negative
    clock_t expected;
    uint local_sec = 43200 - 18000;  // 25200 = 7:00 AM
    CAL_$SEC_TO_CLOCK(&local_sec, &expected);

    // Note: The actual implementation ADDS the offset (utc_delta * 60)
    // So with utc_delta = -300, it adds -18000 seconds
    ASSERT_EQ(result.high, expected.high);
    ASSERT_EQ(result.low, expected.low);
}

// Test: Positive timezone offset (JST = UTC+9 = +540 minutes)
// Local time should be 9 hours ahead of UTC
void test_get_local_time_jst(void) {
    // Setup: JST timezone (UTC+9)
    CAL_$TIMEZONE.utc_delta = 540;  // +9 hours in minutes
    CAL_$TIMEZONE.drift.high = 0;
    CAL_$TIMEZONE.drift.low = 0;

    // Mock system clock at noon UTC
    clock_t utc_noon;
    uint sec = 43200;  // 12:00:00
    CAL_$SEC_TO_CLOCK(&sec, &utc_noon);
    MOCK_TIME_CLOCK(utc_noon);

    clock_t result;
    CAL_$GET_LOCAL_TIME(&result);

    // Expected: 21:00 local (9 hours ahead)
    // +540 minutes * 60 = +32400 seconds offset
    clock_t expected;
    uint local_sec = 43200 + 32400;  // 75600 = 21:00
    CAL_$SEC_TO_CLOCK(&local_sec, &expected);

    ASSERT_EQ(result.high, expected.high);
    ASSERT_EQ(result.low, expected.low);
}

// Test: Drift correction is applied
void test_get_local_time_with_drift(void) {
    // Setup: UTC timezone with positive drift correction
    CAL_$TIMEZONE.utc_delta = 0;
    // Drift of 1 second (250,000 ticks)
    CAL_$TIMEZONE.drift.high = 0x0003;
    CAL_$TIMEZONE.drift.low = 0xD090;

    clock_t system_clock = { .high = 0x1000, .low = 0 };
    MOCK_TIME_CLOCK(system_clock);

    clock_t result;
    CAL_$GET_LOCAL_TIME(&result);

    // Result should be system_clock + drift
    clock_t expected;
    expected.high = system_clock.high + CAL_$TIMEZONE.drift.high;
    expected.low = system_clock.low + CAL_$TIMEZONE.drift.low;

    ASSERT_EQ(result.high, expected.high);
    ASSERT_EQ(result.low, expected.low);
}

// Test: Both timezone offset and drift applied together
void test_get_local_time_offset_and_drift(void) {
    // Setup: +60 minutes offset and small drift
    CAL_$TIMEZONE.utc_delta = 60;  // +1 hour
    CAL_$TIMEZONE.drift.high = 0;
    CAL_$TIMEZONE.drift.low = 1000;  // Small drift

    clock_t system_clock;
    uint sec = 3600;  // 1 hour
    CAL_$SEC_TO_CLOCK(&sec, &system_clock);
    MOCK_TIME_CLOCK(system_clock);

    clock_t result;
    CAL_$GET_LOCAL_TIME(&result);

    // Expected: system_clock + 1 hour offset + drift
    clock_t offset_clock;
    uint offset_sec = 3600;  // 60 minutes in seconds
    CAL_$SEC_TO_CLOCK(&offset_sec, &offset_clock);

    clock_t expected = system_clock;
    ADD48(&expected, &offset_clock);
    ADD48(&expected, &CAL_$TIMEZONE.drift);

    ASSERT_EQ(result.high, expected.high);
    ASSERT_EQ(result.low, expected.low);
}
