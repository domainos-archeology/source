#include "cal.h"

// CAL_$APPLY_LOCAL_OFFSET adds the timezone offset to a clock value.
// It converts utc_delta (minutes) to seconds, then to clock ticks, and adds.
//
// These tests require setting CAL_$TIMEZONE.utc_delta before each test.

// Test: Zero offset (UTC) - clock should be unchanged
void test_apply_local_offset_utc(void) {
    CAL_$TIMEZONE.utc_delta = 0;

    clock_t clock = { .high = 0x12345678, .low = 0xABCD };
    clock_t original = clock;

    CAL_$APPLY_LOCAL_OFFSET(&clock);

    ASSERT_EQ(clock.high, original.high);
    ASSERT_EQ(clock.low, original.low);
}

// Test: Positive offset (+60 minutes = +1 hour)
void test_apply_local_offset_positive_one_hour(void) {
    CAL_$TIMEZONE.utc_delta = 60;  // +1 hour

    clock_t clock = { .high = 0, .low = 0 };

    CAL_$APPLY_LOCAL_OFFSET(&clock);

    // Expected: 60 * 60 = 3600 seconds = 900,000,000 ticks = 0x35A4E900
    ASSERT_EQ(clock.high, 0x35A4);
    ASSERT_EQ(clock.low, 0xE900);
}

// Test: Negative offset (-300 minutes = -5 hours, like EST)
void test_apply_local_offset_negative_est(void) {
    CAL_$TIMEZONE.utc_delta = -300;  // -5 hours

    // Start with a clock value representing 12:00:00 (noon)
    // 12 hours = 43200 seconds
    clock_t clock;
    uint sec = 43200;
    CAL_$SEC_TO_CLOCK(&sec, &clock);
    clock_t original = clock;

    CAL_$APPLY_LOCAL_OFFSET(&clock);

    // Expected: original + (-5 hours) = 7:00:00
    // -300 * 60 = -18000 seconds
    // Adding a negative offset subtracts time
    clock_t offset;
    uint offset_sec = 18000;
    CAL_$SEC_TO_CLOCK(&offset_sec, &offset);

    // Result should be original - offset = 43200 - 18000 = 25200 seconds
    clock_t expected;
    uint expected_sec = 25200;
    CAL_$SEC_TO_CLOCK(&expected_sec, &expected);

    ASSERT_EQ(clock.high, expected.high);
    ASSERT_EQ(clock.low, expected.low);
}

// Test: Large positive offset (+540 minutes = +9 hours, like JST)
void test_apply_local_offset_positive_jst(void) {
    CAL_$TIMEZONE.utc_delta = 540;  // +9 hours

    clock_t clock = { .high = 0, .low = 0 };

    CAL_$APPLY_LOCAL_OFFSET(&clock);

    // Expected: 540 * 60 = 32400 seconds = 8,100,000,000 ticks
    // 8,100,000,000 = 0x1E2A54400 (exceeds 32 bits)
    // As 48-bit: high = 0x1E2A5, low = 0x4400
    clock_t expected;
    uint sec = 32400;
    CAL_$SEC_TO_CLOCK(&sec, &expected);

    ASSERT_EQ(clock.high, expected.high);
    ASSERT_EQ(clock.low, expected.low);
}

// Test: Half-hour offset (+330 minutes = +5:30, like IST)
void test_apply_local_offset_half_hour(void) {
    CAL_$TIMEZONE.utc_delta = 330;  // +5:30

    clock_t clock = { .high = 0, .low = 0 };

    CAL_$APPLY_LOCAL_OFFSET(&clock);

    // Expected: 330 * 60 = 19800 seconds
    clock_t expected;
    uint sec = 19800;
    CAL_$SEC_TO_CLOCK(&sec, &expected);

    ASSERT_EQ(clock.high, expected.high);
    ASSERT_EQ(clock.low, expected.low);
}

// Test: Offset applied to non-zero clock value
void test_apply_local_offset_to_existing_value(void) {
    CAL_$TIMEZONE.utc_delta = 60;  // +1 hour

    // Start with 1 hour worth of ticks
    clock_t clock;
    uint sec = 3600;
    CAL_$SEC_TO_CLOCK(&sec, &clock);

    CAL_$APPLY_LOCAL_OFFSET(&clock);

    // Expected: 2 hours worth of ticks
    clock_t expected;
    uint expected_sec = 7200;
    CAL_$SEC_TO_CLOCK(&expected_sec, &expected);

    ASSERT_EQ(clock.high, expected.high);
    ASSERT_EQ(clock.low, expected.low);
}
