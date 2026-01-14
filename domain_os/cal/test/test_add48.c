#include "cal.h"

// Test: Adding two small values with no carry
// 0x00000001:0x0000 + 0x00000001:0x0000 = 0x00000002:0x0000
void test_add48_simple_no_carry(void) {
    clock_t a = { .high = 1, .low = 0 };
    clock_t b = { .high = 1, .low = 0 };

    ADD48(&a, &b);

    ASSERT_EQ(a.high, 2);
    ASSERT_EQ(a.low, 0);
}

// Test: Adding values where low words sum without overflow
// 0x00000000:0x1000 + 0x00000000:0x2000 = 0x00000000:0x3000
void test_add48_low_no_overflow(void) {
    clock_t a = { .high = 0, .low = 0x1000 };
    clock_t b = { .high = 0, .low = 0x2000 };

    ADD48(&a, &b);

    ASSERT_EQ(a.high, 0);
    ASSERT_EQ(a.low, 0x3000);
}

// Test: Carry propagation from low to high word
// 0x00000000:0xFFFF + 0x00000000:0x0001 = 0x00000001:0x0000
void test_add48_carry_from_low_to_high(void) {
    clock_t a = { .high = 0, .low = 0xFFFF };
    clock_t b = { .high = 0, .low = 0x0001 };

    ADD48(&a, &b);

    ASSERT_EQ(a.high, 1);
    ASSERT_EQ(a.low, 0);
}

// Test: Carry with non-zero high words
// 0x00000100:0xF000 + 0x00000200:0x2000 = 0x00000301:0x1000
void test_add48_carry_with_high_values(void) {
    clock_t a = { .high = 0x100, .low = 0xF000 };
    clock_t b = { .high = 0x200, .low = 0x2000 };

    ADD48(&a, &b);

    ASSERT_EQ(a.high, 0x301);
    ASSERT_EQ(a.low, 0x1000);
}

// Test: Adding zero (identity operation)
void test_add48_add_zero(void) {
    clock_t a = { .high = 0x12345678, .low = 0xABCD };
    clock_t b = { .high = 0, .low = 0 };

    ADD48(&a, &b);

    ASSERT_EQ(a.high, 0x12345678);
    ASSERT_EQ(a.low, 0xABCD);
}

// Test: Maximum 48-bit value wrap-around (overflow)
// 0xFFFFFFFF:0xFFFF + 0x00000000:0x0001 = 0x00000000:0x0000 (with overflow)
void test_add48_overflow_wraps(void) {
    clock_t a = { .high = 0xFFFFFFFF, .low = 0xFFFF };
    clock_t b = { .high = 0, .low = 1 };

    ADD48(&a, &b);

    // Overflow wraps to zero
    ASSERT_EQ(a.high, 0);
    ASSERT_EQ(a.low, 0);
}
