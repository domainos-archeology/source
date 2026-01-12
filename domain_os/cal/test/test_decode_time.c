#include "../cal.h"

// CAL_$DECODE_TIME converts a 48-bit clock to:
// time_rec[0] = year, [1] = month, [2] = day, [3] = hour, [4] = minute, [5] = second
// Epoch is January 1, 1980 00:00:00

// Helper to create a clock from seconds since epoch
static void seconds_to_clock(uint seconds, clock_t *clock) {
    CAL_$SEC_TO_CLOCK(&seconds, clock);
}

// Test: Epoch - clock value 0 should decode to 1980-01-01 00:00:00
void test_decode_time_epoch(void) {
    clock_t clock = { .high = 0, .low = 0 };
    short time_rec[6];

    CAL_$DECODE_TIME(&clock, time_rec);

    ASSERT_EQ(time_rec[0], 1980);  // year
    ASSERT_EQ(time_rec[1], 1);     // month
    ASSERT_EQ(time_rec[2], 1);     // day
    ASSERT_EQ(time_rec[3], 0);     // hour
    ASSERT_EQ(time_rec[4], 0);     // minute
    ASSERT_EQ(time_rec[5], 0);     // second
}

// Test: One second after epoch
void test_decode_time_one_second(void) {
    clock_t clock;
    short time_rec[6];
    uint sec = 1;

    seconds_to_clock(sec, &clock);
    CAL_$DECODE_TIME(&clock, time_rec);

    ASSERT_EQ(time_rec[0], 1980);
    ASSERT_EQ(time_rec[1], 1);
    ASSERT_EQ(time_rec[2], 1);
    ASSERT_EQ(time_rec[3], 0);
    ASSERT_EQ(time_rec[4], 0);
    ASSERT_EQ(time_rec[5], 1);
}

// Test: One minute after epoch (60 seconds)
void test_decode_time_one_minute(void) {
    clock_t clock;
    short time_rec[6];
    uint sec = 60;

    seconds_to_clock(sec, &clock);
    CAL_$DECODE_TIME(&clock, time_rec);

    ASSERT_EQ(time_rec[0], 1980);
    ASSERT_EQ(time_rec[1], 1);
    ASSERT_EQ(time_rec[2], 1);
    ASSERT_EQ(time_rec[3], 0);
    ASSERT_EQ(time_rec[4], 1);
    ASSERT_EQ(time_rec[5], 0);
}

// Test: One hour after epoch (3600 seconds)
void test_decode_time_one_hour(void) {
    clock_t clock;
    short time_rec[6];
    uint sec = 3600;

    seconds_to_clock(sec, &clock);
    CAL_$DECODE_TIME(&clock, time_rec);

    ASSERT_EQ(time_rec[0], 1980);
    ASSERT_EQ(time_rec[1], 1);
    ASSERT_EQ(time_rec[2], 1);
    ASSERT_EQ(time_rec[3], 1);
    ASSERT_EQ(time_rec[4], 0);
    ASSERT_EQ(time_rec[5], 0);
}

// Test: One day after epoch (86400 seconds)
void test_decode_time_one_day(void) {
    clock_t clock;
    short time_rec[6];
    uint sec = 86400;

    seconds_to_clock(sec, &clock);
    CAL_$DECODE_TIME(&clock, time_rec);

    ASSERT_EQ(time_rec[0], 1980);
    ASSERT_EQ(time_rec[1], 1);
    ASSERT_EQ(time_rec[2], 2);  // January 2nd
    ASSERT_EQ(time_rec[3], 0);
    ASSERT_EQ(time_rec[4], 0);
    ASSERT_EQ(time_rec[5], 0);
}

// Test: End of January 1980 (31 days = 31 * 86400 seconds)
void test_decode_time_end_of_january(void) {
    clock_t clock;
    short time_rec[6];
    uint sec = 31 * 86400;  // January has 31 days

    seconds_to_clock(sec, &clock);
    CAL_$DECODE_TIME(&clock, time_rec);

    ASSERT_EQ(time_rec[0], 1980);
    ASSERT_EQ(time_rec[1], 2);  // February
    ASSERT_EQ(time_rec[2], 1);  // 1st
}

// Test: Leap day February 29, 1980
// 1980 is a leap year. Days to Feb 29: 31 (Jan) + 29 = 60 days
// But day 1 is Jan 1, so Feb 29 is day 60, meaning 59 days after epoch
void test_decode_time_leap_day_1980(void) {
    clock_t clock;
    short time_rec[6];
    uint sec = 59 * 86400;  // 59 days after Jan 1 = Feb 29

    seconds_to_clock(sec, &clock);
    CAL_$DECODE_TIME(&clock, time_rec);

    ASSERT_EQ(time_rec[0], 1980);
    ASSERT_EQ(time_rec[1], 2);   // February
    ASSERT_EQ(time_rec[2], 29);  // 29th (leap day)
}

// Test: March 1, 1980 (day after leap day)
// 60 days after epoch (Jan 1 is day 1, so 60 days later is day 61 = Mar 1)
void test_decode_time_march_first_1980(void) {
    clock_t clock;
    short time_rec[6];
    uint sec = 60 * 86400;

    seconds_to_clock(sec, &clock);
    CAL_$DECODE_TIME(&clock, time_rec);

    ASSERT_EQ(time_rec[0], 1980);
    ASSERT_EQ(time_rec[1], 3);  // March
    ASSERT_EQ(time_rec[2], 1);  // 1st
}

// Test: January 1, 1981 (366 days after epoch, since 1980 is leap year)
void test_decode_time_new_year_1981(void) {
    clock_t clock;
    short time_rec[6];
    uint sec = 366 * 86400;

    seconds_to_clock(sec, &clock);
    CAL_$DECODE_TIME(&clock, time_rec);

    ASSERT_EQ(time_rec[0], 1981);
    ASSERT_EQ(time_rec[1], 1);
    ASSERT_EQ(time_rec[2], 1);
}

// Test: Non-leap year (1981) - Feb 28 should be valid, Feb 29 should be March 1
void test_decode_time_non_leap_year(void) {
    clock_t clock;
    short time_rec[6];
    // Days to Feb 28, 1981: 366 (1980) + 31 (Jan) + 28 = 425 days
    // But we count from day 1, so it's 424 days after epoch
    uint sec = (366 + 31 + 27) * 86400;  // Feb 28, 1981

    seconds_to_clock(sec, &clock);
    CAL_$DECODE_TIME(&clock, time_rec);

    ASSERT_EQ(time_rec[0], 1981);
    ASSERT_EQ(time_rec[1], 2);   // February
    ASSERT_EQ(time_rec[2], 28);  // 28th
}

// Test: Complex time value 1985-07-04 12:30:45
void test_decode_time_specific_datetime(void) {
    clock_t clock;
    short time_rec[6];
    // Days from 1980-01-01 to 1985-07-04:
    // 1980: 366 days (leap)
    // 1981: 365 days
    // 1982: 365 days
    // 1983: 365 days
    // 1984: 366 days (leap)
    // 1985: Jan(31) + Feb(28) + Mar(31) + Apr(30) + May(31) + Jun(30) + 3 days = 184 days
    // Total: 366 + 365 + 365 + 365 + 366 + 184 = 2011 days
    // But Jan 1 1980 is day 1, so July 4 1985 is day 2011, meaning 2010 days after
    // Plus 12:30:45 = 12*3600 + 30*60 + 45 = 45045 seconds
    uint sec = 2010 * 86400 + 45045;

    seconds_to_clock(sec, &clock);
    CAL_$DECODE_TIME(&clock, time_rec);

    ASSERT_EQ(time_rec[0], 1985);
    ASSERT_EQ(time_rec[1], 7);   // July
    ASSERT_EQ(time_rec[2], 4);   // 4th
    ASSERT_EQ(time_rec[3], 12);  // 12 hours
    ASSERT_EQ(time_rec[4], 30);  // 30 minutes
    ASSERT_EQ(time_rec[5], 45);  // 45 seconds
}

// Test: Maximum seconds in a day (23:59:59)
void test_decode_time_end_of_day(void) {
    clock_t clock;
    short time_rec[6];
    // 23:59:59 on Jan 1, 1980 = 86399 seconds
    uint sec = 86399;

    seconds_to_clock(sec, &clock);
    CAL_$DECODE_TIME(&clock, time_rec);

    ASSERT_EQ(time_rec[0], 1980);
    ASSERT_EQ(time_rec[1], 1);
    ASSERT_EQ(time_rec[2], 1);
    ASSERT_EQ(time_rec[3], 23);
    ASSERT_EQ(time_rec[4], 59);
    ASSERT_EQ(time_rec[5], 59);
}
