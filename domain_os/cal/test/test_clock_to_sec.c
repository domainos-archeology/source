#include "cal.h"

// Clock ticks are 4 microseconds each = 250,000 ticks per second
// So to convert ticks to seconds, divide by 250,000

// Test: Convert 0 ticks to 0 seconds
void test_clock_to_sec_zero(void) {
    clock_t clock = { .high = 0, .low = 0 };

    ulong result = CAL_$CLOCK_TO_SEC(&clock);

    ASSERT_EQ(result, 0);
}

// Test: Convert 250,000 ticks (1 second worth)
void test_clock_to_sec_one_second(void) {
    // 250,000 = 0x0003D090
    clock_t clock = { .high = 0x0003, .low = 0xD090 };

    ulong result = CAL_$CLOCK_TO_SEC(&clock);

    ASSERT_EQ(result, 1);
}

// Test: Convert 15,000,000 ticks (60 seconds / 1 minute)
void test_clock_to_sec_one_minute(void) {
    // 15,000,000 = 0x00E4E1C0
    clock_t clock = { .high = 0x00E4, .low = 0xE1C0 };

    ulong result = CAL_$CLOCK_TO_SEC(&clock);

    ASSERT_EQ(result, 60);
}

// Test: Convert 900,000,000 ticks (3600 seconds / 1 hour)
void test_clock_to_sec_one_hour(void) {
    // 900,000,000 = 0x35A4E900
    clock_t clock = { .high = 0x35A4, .low = 0xE900 };

    ulong result = CAL_$CLOCK_TO_SEC(&clock);

    ASSERT_EQ(result, 3600);
}

// Test: Convert 21,600,000,000 ticks (86400 seconds / 1 day)
void test_clock_to_sec_one_day(void) {
    // 21,600,000,000 = 0x050007B580
    clock_t clock = { .high = 0x0507, .low = 0xB580 };

    ulong result = CAL_$CLOCK_TO_SEC(&clock);

    ASSERT_EQ(result, 86400);
}

// Test: Verify round-trip: sec -> clock -> sec
void test_clock_to_sec_round_trip(void) {
    uint original_sec = 12345;
    clock_t clock;

    CAL_$SEC_TO_CLOCK(&original_sec, &clock);
    ulong result = CAL_$CLOCK_TO_SEC(&clock);

    ASSERT_EQ(result, original_sec);
}

// Test: Round-trip with large value
void test_clock_to_sec_round_trip_large(void) {
    uint original_sec = 31536000;  // 1 year in seconds (365 days)
    clock_t clock;

    CAL_$SEC_TO_CLOCK(&original_sec, &clock);
    ulong result = CAL_$CLOCK_TO_SEC(&clock);

    ASSERT_EQ(result, original_sec);
}

// Test: Partial second (ticks that don't make a full second)
// 125,000 ticks = 0.5 seconds, should truncate to 0
void test_clock_to_sec_partial_truncates(void) {
    // 125,000 = 0x0001E848
    clock_t clock = { .high = 0x0001, .low = 0xE848 };

    ulong result = CAL_$CLOCK_TO_SEC(&clock);

    ASSERT_EQ(result, 0);  // Truncates to 0
}

// Test: Just under 2 seconds worth of ticks
void test_clock_to_sec_just_under_two(void) {
    // 499,999 ticks = 1.999996 seconds, should be 1
    // 499,999 = 0x0007A11F
    clock_t clock = { .high = 0x0007, .low = 0xA11F };

    ulong result = CAL_$CLOCK_TO_SEC(&clock);

    ASSERT_EQ(result, 1);
}
