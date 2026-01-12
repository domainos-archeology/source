#include "../cal.h"

// CAL_$REMOVE_LOCAL_OFFSET subtracts the timezone offset from a clock value.
// It converts utc_delta (minutes) to seconds, then to clock ticks, and subtracts.
//
// This is the inverse of CAL_$APPLY_LOCAL_OFFSET.

// Test: Zero offset (UTC) - clock should be unchanged
void test_remove_local_offset_utc(void) {
    CAL_$TIMEZONE.utc_delta = 0;

    clock_t clock = { .high = 0x12345678, .low = 0xABCD };
    clock_t original = clock;

    CAL_$REMOVE_LOCAL_OFFSET(&clock);

    ASSERT_EQ(clock.high, original.high);
    ASSERT_EQ(clock.low, original.low);
}

// Test: Positive offset (+60 minutes) - should subtract 1 hour
void test_remove_local_offset_positive_one_hour(void) {
    CAL_$TIMEZONE.utc_delta = 60;  // +1 hour

    // Start with 2 hours worth of ticks
    clock_t clock;
    uint sec = 7200;
    CAL_$SEC_TO_CLOCK(&sec, &clock);

    CAL_$REMOVE_LOCAL_OFFSET(&clock);

    // Expected: 1 hour worth of ticks
    clock_t expected;
    uint expected_sec = 3600;
    CAL_$SEC_TO_CLOCK(&expected_sec, &expected);

    ASSERT_EQ(clock.high, expected.high);
    ASSERT_EQ(clock.low, expected.low);
}

// Test: Negative offset (-300 minutes = -5 hours)
// Removing a negative offset means adding time
void test_remove_local_offset_negative_est(void) {
    CAL_$TIMEZONE.utc_delta = -300;  // -5 hours

    // Start with 7:00:00 local time
    clock_t clock;
    uint sec = 25200;  // 7 hours in seconds
    CAL_$SEC_TO_CLOCK(&sec, &clock);

    CAL_$REMOVE_LOCAL_OFFSET(&clock);

    // Subtracting -5 hours = adding 5 hours
    // Expected: 12:00:00
    clock_t expected;
    uint expected_sec = 43200;
    CAL_$SEC_TO_CLOCK(&expected_sec, &expected);

    ASSERT_EQ(clock.high, expected.high);
    ASSERT_EQ(clock.low, expected.low);
}

// Test: Round-trip: apply then remove should give original
void test_remove_local_offset_round_trip(void) {
    CAL_$TIMEZONE.utc_delta = 330;  // +5:30 (IST)

    clock_t original;
    uint sec = 50000;
    CAL_$SEC_TO_CLOCK(&sec, &original);
    clock_t clock = original;

    // Apply offset then remove it
    CAL_$APPLY_LOCAL_OFFSET(&clock);
    CAL_$REMOVE_LOCAL_OFFSET(&clock);

    ASSERT_EQ(clock.high, original.high);
    ASSERT_EQ(clock.low, original.low);
}

// Test: Round-trip in reverse order: remove then apply
void test_remove_local_offset_reverse_round_trip(void) {
    CAL_$TIMEZONE.utc_delta = -480;  // -8 hours (PST)

    clock_t original;
    uint sec = 86400;  // 1 day
    CAL_$SEC_TO_CLOCK(&sec, &original);
    clock_t clock = original;

    // Remove offset then apply it
    CAL_$REMOVE_LOCAL_OFFSET(&clock);
    CAL_$APPLY_LOCAL_OFFSET(&clock);

    ASSERT_EQ(clock.high, original.high);
    ASSERT_EQ(clock.low, original.low);
}

// Test: Remove offset from zero clock (may underflow)
void test_remove_local_offset_from_zero(void) {
    CAL_$TIMEZONE.utc_delta = 60;  // +1 hour

    clock_t clock = { .high = 0, .low = 0 };

    CAL_$REMOVE_LOCAL_OFFSET(&clock);

    // This will underflow, wrapping to a large negative (as unsigned)
    // The 48-bit value should wrap around
    // 0 - 3600 seconds worth of ticks = large positive number
    ASSERT_TRUE(clock.high > 0 || clock.low > 0);  // Not zero anymore
}
