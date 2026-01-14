#include "cal.h"

// CAL_$GET_INFO copies the global timezone record to a caller-provided buffer

// Test: Get info with default/zero values
void test_get_info_zero_values(void) {
    // Set up known global state
    CAL_$TIMEZONE.utc_delta = 0;
    CAL_$TIMEZONE.tz_name[0] = 'U';
    CAL_$TIMEZONE.tz_name[1] = 'T';
    CAL_$TIMEZONE.tz_name[2] = 'C';
    CAL_$TIMEZONE.tz_name[3] = '\0';
    CAL_$TIMEZONE.drift.high = 0;
    CAL_$TIMEZONE.drift.low = 0;

    cal_$timezone_rec_t info;
    CAL_$GET_INFO(&info);

    ASSERT_EQ(info.utc_delta, 0);
    ASSERT_EQ(info.tz_name[0], 'U');
    ASSERT_EQ(info.tz_name[1], 'T');
    ASSERT_EQ(info.tz_name[2], 'C');
    ASSERT_EQ(info.tz_name[3], '\0');
    ASSERT_EQ(info.drift.high, 0);
    ASSERT_EQ(info.drift.low, 0);
}

// Test: Get info with EST timezone
void test_get_info_est(void) {
    CAL_$TIMEZONE.utc_delta = -300;  // EST = UTC-5
    CAL_$TIMEZONE.tz_name[0] = 'E';
    CAL_$TIMEZONE.tz_name[1] = 'S';
    CAL_$TIMEZONE.tz_name[2] = 'T';
    CAL_$TIMEZONE.tz_name[3] = '\0';
    CAL_$TIMEZONE.drift.high = 0x100;
    CAL_$TIMEZONE.drift.low = 0x200;

    cal_$timezone_rec_t info;
    CAL_$GET_INFO(&info);

    ASSERT_EQ(info.utc_delta, -300);
    ASSERT_EQ(info.tz_name[0], 'E');
    ASSERT_EQ(info.tz_name[1], 'S');
    ASSERT_EQ(info.tz_name[2], 'T');
    ASSERT_EQ(info.tz_name[3], '\0');
    ASSERT_EQ(info.drift.high, 0x100);
    ASSERT_EQ(info.drift.low, 0x200);
}

// Test: Get info copies, doesn't alias
// Modifying the returned struct shouldn't affect global
void test_get_info_is_copy(void) {
    CAL_$TIMEZONE.utc_delta = 60;
    CAL_$TIMEZONE.tz_name[0] = 'A';
    CAL_$TIMEZONE.tz_name[1] = 'B';
    CAL_$TIMEZONE.tz_name[2] = 'C';
    CAL_$TIMEZONE.tz_name[3] = 'D';
    CAL_$TIMEZONE.drift.high = 0x1234;
    CAL_$TIMEZONE.drift.low = 0x5678;

    cal_$timezone_rec_t info;
    CAL_$GET_INFO(&info);

    // Modify the copy
    info.utc_delta = 999;
    info.tz_name[0] = 'X';
    info.drift.high = 0xFFFF;

    // Global should be unchanged
    ASSERT_EQ(CAL_$TIMEZONE.utc_delta, 60);
    ASSERT_EQ(CAL_$TIMEZONE.tz_name[0], 'A');
    ASSERT_EQ(CAL_$TIMEZONE.drift.high, 0x1234);
}

// Test: Get info with positive offset (JST)
void test_get_info_jst(void) {
    CAL_$TIMEZONE.utc_delta = 540;  // JST = UTC+9
    CAL_$TIMEZONE.tz_name[0] = 'J';
    CAL_$TIMEZONE.tz_name[1] = 'S';
    CAL_$TIMEZONE.tz_name[2] = 'T';
    CAL_$TIMEZONE.tz_name[3] = '\0';
    CAL_$TIMEZONE.drift.high = 0;
    CAL_$TIMEZONE.drift.low = 0;

    cal_$timezone_rec_t info;
    CAL_$GET_INFO(&info);

    ASSERT_EQ(info.utc_delta, 540);
    ASSERT_EQ(info.tz_name[0], 'J');
    ASSERT_EQ(info.tz_name[1], 'S');
    ASSERT_EQ(info.tz_name[2], 'T');
}

// Test: Get info with non-ASCII timezone name characters
void test_get_info_special_chars(void) {
    CAL_$TIMEZONE.utc_delta = 0;
    CAL_$TIMEZONE.tz_name[0] = (char)0xA1;  // High ASCII
    CAL_$TIMEZONE.tz_name[1] = (char)0xB2;
    CAL_$TIMEZONE.tz_name[2] = (char)0xC3;
    CAL_$TIMEZONE.tz_name[3] = (char)0xD4;
    CAL_$TIMEZONE.drift.high = 0;
    CAL_$TIMEZONE.drift.low = 0;

    cal_$timezone_rec_t info;
    CAL_$GET_INFO(&info);

    ASSERT_EQ((unsigned char)info.tz_name[0], 0xA1);
    ASSERT_EQ((unsigned char)info.tz_name[1], 0xB2);
    ASSERT_EQ((unsigned char)info.tz_name[2], 0xC3);
    ASSERT_EQ((unsigned char)info.tz_name[3], 0xD4);
}
