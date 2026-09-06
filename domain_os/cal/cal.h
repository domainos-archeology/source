#ifndef CAL_H
#define CAL_H

#include "arch/arch.h"
#include "base/base.h"
#include "math/math.h"
#include "time/time.h"

/* status_$t and status_$ok are defined in base/base.h */
#define status_$cal_refused 0x150007
#define status_$cal_date_or_time_invalid 0x150002

// Timezone record structure (12 bytes total at 0x00e7b030)
typedef struct {
  short utc_delta;  // offset from UTC in minutes (+0)
  char tz_name[4];  // timezone name, e.g. "EST" (+2)
  clock_t drift;    // drift correction (+6)
  ushort boot_volx; // boot volume index (+12)
} cal_$timezone_rec_t;

// Days per month lookup table
extern short CAL_$DAYS_PER_MONTH[12];

// Global timezone data at 0x00e7b030
extern cal_$timezone_rec_t CAL_$TIMEZONE;

// Boot volume index - separate variable that mirrors CAL_$TIMEZONE.boot_volx
// This exists as a separate variable because many files use it with extern declarations
extern int16_t CAL_$BOOT_VOLX;

// Last valid time (high word of clock) at 0x00e7b03c
extern uint CAL_$LAST_VALID_TIME;

/*
 * ===========================================================================
 * OKI MSM5832 real-time clock / calendar
 * ===========================================================================
 *
 * The MSM5832 is a CMOS RTC with 13 four-bit BCD registers selected by a
 * 4-bit address (A3-A0) and read/written over a 4-bit data bus (D3-D0).
 *
 *   Addr  Reg   Contents             Flag bits
 *   ----  ----  -------------------  ----------------------------------
 *    12   Y10   Year tens (0-9)
 *    11   Y1    Year ones (0-9)
 *    10   MO10  Month tens (0-1)
 *     9   MO1   Month ones (0-9)
 *     8   D10   Date tens (0-3)      D2 = leap year (see the note below)
 *     7   D1    Date ones (0-9)
 *     6   W     Weekday (0-6)
 *     5   H10   Hour tens (0-2)      D2 = PM, D3 = 24-hour select
 *     4   H1    Hour ones (0-9)
 *     3   MI10  Minute tens (0-5)
 *     2   MI1   Minute ones (0-9)
 *     1   S10   Second tens (0-5)
 *     0   S1    Second ones (0-9)
 *
 * Apollo interface, three byte-wide ports in the I/O page:
 *
 *   base + 0x20  CONTROL (write only)
 *                bits 7-4  A3-A0, the MSM5832 register address
 *                bit  2    READ  pin
 *                bit  1    WRITE pin
 *                bit  0    HOLD  pin (freezes the counter chain)
 *                CS is produced by the address decoder.
 *   base + 0x22  WRITE DATA (write only)  D3-D0 towards the MSM5832
 *   base + 0x24  READ DATA  (read only)   D3-D0 from the MSM5832
 *
 * The interface hardware inverts both data paths, so software complements
 * every digit it reads or writes (`not.b`).
 *
 * TIME_$READ_CAL loads the base from the constant long at 0x00E2AF66
 * (0x00FFA800) and uses (0x20,A0)/(0x22,A0)/(0x24,A0); CAL_$WRITE_CALENDAR
 * spells the same three addresses out absolutely (0x00FFA820/22/24).
 */
#define CAL_$RTC_BASE               0x00FFA800u  /* long at 0x00E2AF66 */
#define CAL_$RTC_CONTROL_OFFSET     0x20         /* write only */
#define CAL_$RTC_WRITE_DATA_OFFSET  0x22         /* write only */
#define CAL_$RTC_READ_DATA_OFFSET   0x24         /* read only  */

/* MSM5832 register addresses (control-byte bits 7..4) */
#define MSM5832_REG_S1      0
#define MSM5832_REG_S10     1
#define MSM5832_REG_MI1     2
#define MSM5832_REG_MI10    3
#define MSM5832_REG_H1      4
#define MSM5832_REG_H10     5
#define MSM5832_REG_W       6
#define MSM5832_REG_D1      7
#define MSM5832_REG_D10     8
#define MSM5832_REG_MO1     9
#define MSM5832_REG_MO10   10
#define MSM5832_REG_Y1     11
#define MSM5832_REG_Y10    12
#define MSM5832_NUM_REGS   13

/* Apollo control-register signal bits */
#define CAL_$RTC_CTL_HOLD        0x01
#define CAL_$RTC_CTL_WRITE       0x02
#define CAL_$RTC_CTL_READ        0x04
#define CAL_$RTC_CTL_ADDR_SHIFT  4
#define CAL_$RTC_CTL_ADDR_STEP   0x10  /* one register address */

#define CAL_$RTC_CTL_ADDR_HOLD(a) \
  (((a) << CAL_$RTC_CTL_ADDR_SHIFT) | CAL_$RTC_CTL_HOLD)
#define CAL_$RTC_CTL_READ_HOLD(a) \
  (((a) << CAL_$RTC_CTL_ADDR_SHIFT) | CAL_$RTC_CTL_READ | CAL_$RTC_CTL_HOLD)
#define CAL_$RTC_CTL_WRITE_HOLD(a) \
  (((a) << CAL_$RTC_CTL_ADDR_SHIFT) | CAL_$RTC_CTL_WRITE | CAL_$RTC_CTL_HOLD)

/* Digit masks within the flag-carrying registers */
#define MSM5832_D10_LEAP_FLAG 0x04 /* D10 bit 2: leap year (read side) */
#define MSM5832_H10_24H_FLAG  0x08 /* H10 bit 3: 24-hour select        */
#define MSM5832_TENS_MASK     0x03 /* D0-D1 of a flag-carrying tens reg */

/*
 * Register access, isolated behind the arch layer.  On the target these are
 * plain volatile MMIO byte accesses at fixed addresses; on the host build
 * (unit tests) they route through functions the test supplies, so a test can
 * observe the exact control/data sequence the driver emits.
 */
#if defined(ARCH_M68K)
#define CAL_$RTC_WRITE(off, val)                                              \
  (*(volatile uint8_t *)(uintptr_t)(CAL_$RTC_BASE + (off)) = (uint8_t)(val))
#define CAL_$RTC_READ(off)                                                    \
  (*(volatile uint8_t *)(uintptr_t)(CAL_$RTC_BASE + (off)))
#else
uint8_t cal_$rtc_read_reg(uint16_t offset);
void cal_$rtc_write_reg(uint16_t offset, uint8_t value);
#define CAL_$RTC_WRITE(off, val)                                              \
  cal_$rtc_write_reg((uint16_t)(off), (uint8_t)(val))
#define CAL_$RTC_READ(off) cal_$rtc_read_reg((uint16_t)(off))
#endif

#define CAL_$RTC_WRITE_CONTROL(v)                                             \
  CAL_$RTC_WRITE(CAL_$RTC_CONTROL_OFFSET, (v))
#define CAL_$RTC_WRITE_DATA(v)                                                \
  CAL_$RTC_WRITE(CAL_$RTC_WRITE_DATA_OFFSET, (v))
#define CAL_$RTC_READ_DATA() CAL_$RTC_READ(CAL_$RTC_READ_DATA_OFFSET)

// 48-bit arithmetic
extern void ADD48(clock_t *dst, clock_t *src);
// Returns -1 if result is non-negative, 0 if result is negative
extern int8_t SUB48(clock_t *dst, clock_t *src);

// CAL functions
extern void CAL_$SEC_TO_CLOCK(uint *sec, clock_t *clock_ret);
extern ulong CAL_$CLOCK_TO_SEC(clock_t *clock);
extern void CAL_$APPLY_LOCAL_OFFSET(clock_t *clock);
extern void CAL_$REMOVE_LOCAL_OFFSET(clock_t *clock);
extern void CAL_$GET_LOCAL_TIME(clock_t *clock);
extern short CAL_$WEEKDAY(short *year, short *month, short *day);
extern void CAL_$DECODE_TIME(clock_t *clock, short *time_rec);
extern void CAL_$GET_INFO(cal_$timezone_rec_t *info);
extern void CAL_$SET_DRIFT(clock_t *drift);
extern void CAL_$READ_TIMEZONE(cal_$timezone_rec_t *tz, status_$t *status);
extern void CAL_$WRITE_TIMEZONE(cal_$timezone_rec_t *tz, status_$t *status);
extern void CAL_$SHUTDOWN(status_$t *status);
extern char CAL_$VERIFY(int *max_allowed_delta, void *param_2, char *param_3,
                        status_$t *status);
extern void CAL_$WRITE_CALENDAR(int16_t *year, int16_t *month, int16_t *day,
                                int16_t *weekday, int16_t *hour,
                                int16_t *minute, int16_t *second);
extern void CAL_$SETUP_CALLBACK(void *callback_queue);

#endif /* CAL_H */
