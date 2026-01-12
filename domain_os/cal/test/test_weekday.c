#include "../cal.h"

// CAL_$WEEKDAY returns: 0=Sunday, 1=Monday, ..., 6=Saturday

// Test: Known date - January 1, 1980 was a Tuesday
void test_weekday_epoch(void) {
    short year = 1980;
    short month = 1;
    short day = 1;

    short result = CAL_$WEEKDAY(&year, &month, &day);

    ASSERT_EQ(result, 2);  // Tuesday
}

// Test: Known date - July 20, 1969 (Moon landing) was a Sunday
void test_weekday_moon_landing(void) {
    short year = 1969;
    short month = 7;
    short day = 20;

    short result = CAL_$WEEKDAY(&year, &month, &day);

    ASSERT_EQ(result, 0);  // Sunday
}

// Test: Known date - December 25, 1985 was a Wednesday
void test_weekday_christmas_1985(void) {
    short year = 1985;
    short month = 12;
    short day = 25;

    short result = CAL_$WEEKDAY(&year, &month, &day);

    ASSERT_EQ(result, 3);  // Wednesday
}

// Test: January date (tests year adjustment for month < 3)
// January 15, 1984 was a Sunday
void test_weekday_january(void) {
    short year = 1984;
    short month = 1;
    short day = 15;

    short result = CAL_$WEEKDAY(&year, &month, &day);

    ASSERT_EQ(result, 0);  // Sunday
}

// Test: February date (tests year adjustment for month < 3)
// February 29, 1984 was a Wednesday (leap year)
void test_weekday_february_leap_year(void) {
    short year = 1984;
    short month = 2;
    short day = 29;

    short result = CAL_$WEEKDAY(&year, &month, &day);

    ASSERT_EQ(result, 3);  // Wednesday
}

// Test: March 1st (first day after year adjustment threshold)
// March 1, 1984 was a Thursday
void test_weekday_march_first(void) {
    short year = 1984;
    short month = 3;
    short day = 1;

    short result = CAL_$WEEKDAY(&year, &month, &day);

    ASSERT_EQ(result, 4);  // Thursday
}

// Test: Century year that IS a leap year (2000)
// February 29, 2000 was a Tuesday
void test_weekday_century_leap_year(void) {
    short year = 2000;
    short month = 2;
    short day = 29;

    short result = CAL_$WEEKDAY(&year, &month, &day);

    ASSERT_EQ(result, 2);  // Tuesday
}

// Test: Century year that is NOT a leap year (1900)
// March 1, 1900 was a Thursday
void test_weekday_century_non_leap_year(void) {
    short year = 1900;
    short month = 3;
    short day = 1;

    short result = CAL_$WEEKDAY(&year, &month, &day);

    ASSERT_EQ(result, 4);  // Thursday
}

// Test: End of year - December 31, 1999 was a Friday
void test_weekday_end_of_year(void) {
    short year = 1999;
    short month = 12;
    short day = 31;

    short result = CAL_$WEEKDAY(&year, &month, &day);

    ASSERT_EQ(result, 5);  // Friday
}

// Test: Verify all days of a week are consecutive
// Week of January 1-7, 1984: Sun(1), Mon(2), Tue(3), Wed(4), Thu(5), Fri(6), Sat(7)
void test_weekday_consecutive_days(void) {
    short year = 1984;
    short month = 1;

    // January 1, 1984 was a Sunday
    short day = 1;
    ASSERT_EQ(CAL_$WEEKDAY(&year, &month, &day), 0);  // Sunday

    day = 2;
    ASSERT_EQ(CAL_$WEEKDAY(&year, &month, &day), 1);  // Monday

    day = 3;
    ASSERT_EQ(CAL_$WEEKDAY(&year, &month, &day), 2);  // Tuesday

    day = 4;
    ASSERT_EQ(CAL_$WEEKDAY(&year, &month, &day), 3);  // Wednesday

    day = 5;
    ASSERT_EQ(CAL_$WEEKDAY(&year, &month, &day), 4);  // Thursday

    day = 6;
    ASSERT_EQ(CAL_$WEEKDAY(&year, &month, &day), 5);  // Friday

    day = 7;
    ASSERT_EQ(CAL_$WEEKDAY(&year, &month, &day), 6);  // Saturday
}
