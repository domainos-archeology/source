#include "cal.h"

// Clock ticks are 4 microseconds each = 250,000 ticks per second
// So 1 second = 0x0003D090 ticks

// Test: Convert 0 seconds
void test_sec_to_clock_zero(void) {
    uint sec = 0;
    clock_t result;

    CAL_$SEC_TO_CLOCK(&sec, &result);

    ASSERT_EQ(result.high, 0);
    ASSERT_EQ(result.low, 0);
}

// Test: Convert 1 second = 250,000 ticks = 0x0003D090
void test_sec_to_clock_one_second(void) {
    uint sec = 1;
    clock_t result;

    CAL_$SEC_TO_CLOCK(&sec, &result);

    // 250,000 = 0x0003D090
    // high = 0x0003, low = 0xD090
    ASSERT_EQ(result.high, 0x0003);
    ASSERT_EQ(result.low, 0xD090);
}

// Test: Convert 60 seconds (1 minute) = 15,000,000 ticks = 0x00E4E1C0
void test_sec_to_clock_one_minute(void) {
    uint sec = 60;
    clock_t result;

    CAL_$SEC_TO_CLOCK(&sec, &result);

    // 60 * 250,000 = 15,000,000 = 0x00E4E1C0
    // high = 0x00E4, low = 0xE1C0
    ASSERT_EQ(result.high, 0x00E4);
    ASSERT_EQ(result.low, 0xE1C0);
}

// Test: Convert 3600 seconds (1 hour) = 900,000,000 ticks = 0x35A4E900
void test_sec_to_clock_one_hour(void) {
    uint sec = 3600;
    clock_t result;

    CAL_$SEC_TO_CLOCK(&sec, &result);

    // 3600 * 250,000 = 900,000,000 = 0x35A4E900
    // high = 0x35A4, low = 0xE900
    ASSERT_EQ(result.high, 0x35A4);
    ASSERT_EQ(result.low, 0xE900);
}

// Test: Convert 86400 seconds (1 day) = 21,600,000,000 ticks
void test_sec_to_clock_one_day(void) {
    uint sec = 86400;
    clock_t result;

    CAL_$SEC_TO_CLOCK(&sec, &result);

    // 86400 * 250,000 = 21,600,000,000 = 0x507B5800
    // This exceeds 32 bits, so it uses the 48-bit representation
    // 0x0005:07B58000 in 48-bit format
    ASSERT_EQ(result.high, 0x0507);
    ASSERT_EQ(result.low, 0xB580);
}

// Test: Large value near max 32-bit seconds
// Testing with 0x10000 seconds to verify partial product algorithm
void test_sec_to_clock_large_value(void) {
    uint sec = 0x10000;  // 65536 seconds
    clock_t result;

    CAL_$SEC_TO_CLOCK(&sec, &result);

    // 65536 * 250,000 = 16,384,000,000 = 0x3D0900000
    // In 48-bit: high = 0x0003D090, low = 0x0000
    ASSERT_EQ(result.high, 0x0003D090);
    ASSERT_EQ(result.low, 0x0000);
}

// Test: Value that exercises both high and low partial products
void test_sec_to_clock_mixed_bits(void) {
    uint sec = 0x10001;  // 65537 seconds
    clock_t result;

    CAL_$SEC_TO_CLOCK(&sec, &result);

    // 65537 * 250,000 = 16,384,250,000 = 0x3D0930D090
    // In 48-bit: high = 0x0003D093, low = 0xD090
    ASSERT_EQ(result.high, 0x0003D093);
    ASSERT_EQ(result.low, 0xD090);
}
