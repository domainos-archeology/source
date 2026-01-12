#include "../cal.h"

// Test: Simple subtraction with no borrow
// 0x00000002:0x0000 - 0x00000001:0x0000 = 0x00000001:0x0000
void test_sub48_simple_no_borrow(void) {
    clock_t a = { .high = 2, .low = 0 };
    clock_t b = { .high = 1, .low = 0 };

    SUB48(&a, &b);

    ASSERT_EQ(a.high, 1);
    ASSERT_EQ(a.low, 0);
}

// Test: Subtraction in low word only
// 0x00000000:0x3000 - 0x00000000:0x1000 = 0x00000000:0x2000
void test_sub48_low_only(void) {
    clock_t a = { .high = 0, .low = 0x3000 };
    clock_t b = { .high = 0, .low = 0x1000 };

    SUB48(&a, &b);

    ASSERT_EQ(a.high, 0);
    ASSERT_EQ(a.low, 0x2000);
}

// Test: Borrow propagation from high to low word
// 0x00000001:0x0000 - 0x00000000:0x0001 = 0x00000000:0xFFFF
void test_sub48_borrow_from_high(void) {
    clock_t a = { .high = 1, .low = 0 };
    clock_t b = { .high = 0, .low = 1 };

    SUB48(&a, &b);

    ASSERT_EQ(a.high, 0);
    ASSERT_EQ(a.low, 0xFFFF);
}

// Test: Subtraction with borrow and non-zero high words
// 0x00000301:0x1000 - 0x00000200:0x2000 = 0x00000100:0xF000
void test_sub48_borrow_with_high_values(void) {
    clock_t a = { .high = 0x301, .low = 0x1000 };
    clock_t b = { .high = 0x200, .low = 0x2000 };

    SUB48(&a, &b);

    ASSERT_EQ(a.high, 0x100);
    ASSERT_EQ(a.low, 0xF000);
}

// Test: Subtracting zero (identity operation)
void test_sub48_subtract_zero(void) {
    clock_t a = { .high = 0x12345678, .low = 0xABCD };
    clock_t b = { .high = 0, .low = 0 };

    SUB48(&a, &b);

    ASSERT_EQ(a.high, 0x12345678);
    ASSERT_EQ(a.low, 0xABCD);
}

// Test: Subtracting equal values gives zero
void test_sub48_equal_gives_zero(void) {
    clock_t a = { .high = 0x12345678, .low = 0xABCD };
    clock_t b = { .high = 0x12345678, .low = 0xABCD };

    SUB48(&a, &b);

    ASSERT_EQ(a.high, 0);
    ASSERT_EQ(a.low, 0);
}

// Test: Underflow (subtracting larger from smaller wraps)
// 0x00000000:0x0000 - 0x00000000:0x0001 = 0xFFFFFFFF:0xFFFF (underflow)
void test_sub48_underflow_wraps(void) {
    clock_t a = { .high = 0, .low = 0 };
    clock_t b = { .high = 0, .low = 1 };

    SUB48(&a, &b);

    // Underflow wraps to max value
    ASSERT_EQ(a.high, 0xFFFFFFFF);
    ASSERT_EQ(a.low, 0xFFFF);
}
