/*
 * smd/test/test_validate_unit.c - Unit tests for smd_$validate_unit
 *
 * Tests that display unit validation works correctly:
 * - Unit 1 with configured display returns valid (0xFF)
 * - Unit 1 with unconfigured display returns invalid (0)
 * - All other unit numbers return invalid (0)
 */

#include <stdio.h>
#include <assert.h>
#include <stdint.h>
#include <string.h>

/* Minimal type stubs for native compilation */
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
        printf("FAILED\n    Expected: 0x%x, Got: 0x%x at line %d\n", \
               (unsigned)(expected), (unsigned)(actual), __LINE__); \
        tests_failed++; \
        return; \
    } \
} while(0)

/*
 * Mock data structures - minimal versions for testing
 */
typedef struct {
    uint16_t display_type;
    uint8_t pad[0x5E]; /* rest of 0x60 bytes */
} mock_display_info_t;

/* Mock globals */
mock_display_info_t SMD_DISPLAY_INFO[4];

/* Function under test - declared to match original */
int8_t smd_$validate_unit(uint16_t unit);

/* Implementation under test (included directly for unit testing) */
int8_t smd_$validate_unit(uint16_t unit)
{
    if (unit == 1) {
        return (SMD_DISPLAY_INFO[unit - 1].display_type != 0) ? (int8_t)0xFF : 0;
    }
    return 0;
}

/*
 * Tests
 */

TEST(unit1_configured)
{
    SMD_DISPLAY_INFO[0].display_type = 1; /* Mono landscape */
    int8_t result = smd_$validate_unit(1);
    ASSERT_EQ((int8_t)0xFF, result);
}

TEST(unit1_unconfigured)
{
    SMD_DISPLAY_INFO[0].display_type = 0; /* Not configured */
    int8_t result = smd_$validate_unit(1);
    ASSERT_EQ(0, result);
}

TEST(unit0_invalid)
{
    int8_t result = smd_$validate_unit(0);
    ASSERT_EQ(0, result);
}

TEST(unit2_invalid)
{
    int8_t result = smd_$validate_unit(2);
    ASSERT_EQ(0, result);
}

TEST(unit3_invalid)
{
    int8_t result = smd_$validate_unit(3);
    ASSERT_EQ(0, result);
}

TEST(unit_max_invalid)
{
    int8_t result = smd_$validate_unit(0xFFFF);
    ASSERT_EQ(0, result);
}

TEST(unit1_various_types)
{
    /* Test various display types are all considered valid */
    uint16_t types[] = { 1, 2, 3, 4, 5, 6, 8, 9, 10, 11 };
    for (int i = 0; i < (int)(sizeof(types) / sizeof(types[0])); i++) {
        SMD_DISPLAY_INFO[0].display_type = types[i];
        int8_t result = smd_$validate_unit(1);
        ASSERT_EQ((int8_t)0xFF, result);
    }
}

int main(void)
{
    printf("test_validate_unit:\n");

    RUN_TEST(unit1_configured);
    RUN_TEST(unit1_unconfigured);
    RUN_TEST(unit0_invalid);
    RUN_TEST(unit2_invalid);
    RUN_TEST(unit3_invalid);
    RUN_TEST(unit_max_invalid);
    RUN_TEST(unit1_various_types);

    printf("\n  Results: %d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
