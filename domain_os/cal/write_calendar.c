#include "cal.h"
#include "network/network.h"

// Internal helper functions (static, not exported)
static void cal_$delay(short count);
static void cal_$delay_40(void);
static void cal_$write_calendar_digit(short control, unsigned char digit);
static void cal_$write_calendar_0_to_99(short control, short value);
static void cal_$finish_write(void);

// Simple delay loop
static void cal_$delay(short count) {
    while (count >= 0) {
        count--;
    }
}

// Delay for 40 iterations
static void cal_$delay_40(void) {
    cal_$delay(40);
}

// Writes a single BCD digit to the hardware calendar chip.
// The digit is inverted before being written to the data register.
// Control lines are toggled with delays for chip timing requirements.
static void cal_$write_calendar_digit(short control, unsigned char digit) {
    CAL_$WRITE_DATA_VIRTUAL_ADDR = ~digit;
    CAL_$CONTROL_VIRTUAL_ADDR = (char)control;
    cal_$delay_40();

    CAL_$CONTROL_VIRTUAL_ADDR = (char)(control | 2);
    cal_$delay_40();

    CAL_$CONTROL_VIRTUAL_ADDR = (char)control;
    cal_$delay_40();
}

// Writes a two-digit BCD value (0-99) to the hardware calendar chip.
// Splits the value into tens and units digits.
static void cal_$write_calendar_0_to_99(short control, short value) {
    cal_$write_calendar_digit(control, (unsigned char)(value / 10));
    cal_$write_calendar_digit(control - 0x10, (unsigned char)(value % 10));
}

// Finishes the calendar write operation by clearing control lines.
static void cal_$finish_write(void) {
    CAL_$CONTROL_VIRTUAL_ADDR = 0;
}

// Writes date and time to the hardware real-time clock chip.
//
// This function programs the Dallas/National Semiconductor style RTC chip
// used in Apollo workstations. The chip is accessed through memory-mapped
// I/O at addresses 0xFFA820 (control) and 0xFFA822 (data).
//
// Parameters are all passed by reference:
//   year   - 4-digit year (e.g., 1985)
//   month  - 1-12
//   day    - 1-31
//   weekday - 0-6 (not used in BCD encoding directly)
//   hour   - 0-23
//   minute - 0-59
//   second - 0-59
//
// The chip stores year as 2 digits (00-99), with leap year handling
// based on whether the year mod 4 == 0.
void CAL_$WRITE_CALENDAR(short *year, short *month, short *day, short *weekday,
                          short *hour, short *minute, short *second) {
    short year_2digit;
    short month_val;
    short day_val;

    // Only write if we have a real disk (not diskless boot)
    if (NETWORK_$REALLY_DISKLESS != 0) {
        return;
    }

    // Start write sequence
    CAL_$CONTROL_VIRTUAL_ADDR = 1;
    cal_$delay(200);

    // Get 2-digit year
    year_2digit = (ushort)((uint)(int)*year % 100);

    // Write year (2 digits)
    cal_$write_calendar_0_to_99(0xC1, year_2digit);

    // Write month
    month_val = *month;
    cal_$write_calendar_0_to_99(0xB1, month_val);

    // Write day, with leap year adjustment
    // If month > 2 and it's a leap year, add 0x50 to day
    // This encodes the leap year flag in the day field
    day_val = *day;
    if (month_val > 2) {
        year_2digit++;
        if (year_2digit > 99) {
            year_2digit = 0;
        }
    }
    if ((year_2digit & 3) == 0) {
        day_val += 0x50;
    }
    cal_$write_calendar_0_to_99(0xA1, day_val);

    // Write weekday (single digit)
    cal_$write_calendar_digit(0x91, (unsigned char)*weekday);

    // Write hour (with 0x50 offset for 24-hour format flag)
    cal_$write_calendar_0_to_99(0x81, *hour + 0x50);

    // Write minute
    cal_$write_calendar_0_to_99(0x71, *minute);

    // Write second
    cal_$write_calendar_0_to_99(0x61, *second);

    cal_$finish_write();
}
