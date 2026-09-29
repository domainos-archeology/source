#ifndef CAL_H
#define CAL_H

#include "arch/arch.h"
#include "base/base.h"
#include "math/math.h"
#include "time/time.h"

/* status_$t and status_$ok are defined in base/base.h */
/*
 * 0x150007 is the code CAL_$VERIFY stores at 0x00E68462 when the operator
 * answers 'n' to the calendar prompt.  Neither the SR10.2 nor the SR10.4
 * status-code table (~/src/domainos-archeology/stcodes) has text for
 * module 0x15 code 7 (they stop at 150003 / 150005), so this name is ours.
 */
#define status_$cal_refused 0x150007
/* 150002 "date or time specification invalid" (SR10.2 stcode table) */
#define status_$cal_date_or_time_invalid 0x150002

/*
 * cal_$timezone_rec_t - the 12-byte timezone record
 *
 * CAL_$GET_INFO (0x00E68576), CAL_$READ_TIMEZONE (0x00E3E5CE) and
 * CAL_$WRITE_TIMEZONE (0x00E3E62A) all move it as exactly three longwords,
 * and NETWORK's diskless fetch copies the same 12 bytes into it.
 */
typedef struct cal_$timezone_rec_t {
  int16_t utc_delta;  /* 0x00: offset from UTC in minutes; sign-extended by
                       *       `ext.l` at 0x00E68606 before the *60 */
  char tz_name[4];    /* 0x02: timezone name, e.g. "EST " */
  clock_t drift;      /* 0x06: drift correction, ADD48'd by CAL_$GET_LOCAL_TIME
                       *       (`pea (0xe7b036).l` at 0x00E685E2) */
} cal_$timezone_rec_t;

_Static_assert(__builtin_offsetof(cal_$timezone_rec_t, utc_delta) == 0x00, "cal_$timezone_rec_t.utc_delta");
_Static_assert(__builtin_offsetof(cal_$timezone_rec_t, tz_name) == 0x02, "cal_$timezone_rec_t.tz_name");
_Static_assert(__builtin_offsetof(cal_$timezone_rec_t, drift) == 0x06, "cal_$timezone_rec_t.drift");
_Static_assert(sizeof(cal_$timezone_rec_t) == 12, "cal_$timezone_rec_t: three longword moves");

/*
 * Days per month, the 12 words at 0x00E817AC (map: CAL_$DAYS_PER_MONTH,
 * inside the CAL_ code segment between CAL_$SEC_TO_CLOCK and
 * CAL_$CLOCK_TO_SEC).  CAL_$DECODE_TIME copies it into a frame local.
 */
extern short CAL_$DAYS_PER_MONTH[12];

/*
 * The OS_CAL_WIRED data segment (map: D E7B030 OS_CAL_WIRED size = 14).
 * CAL_$READ_TIMEZONE / CAL_$WRITE_TIMEZONE address it through A5 = 0xE7B030:
 *
 *   +0x00  CAL_$TIMEZONE         cal_$timezone_rec_t, 12 bytes
 *   +0x0C  CAL_$LAST_VALID_TIME  longword (`move.l (0xe6,A4),(0xc,A5)`)
 *   +0x10  CAL_$BOOT_VOLX        word     (`move.w (0x10,A5),(-0xa,A6)`)
 *
 * Nothing in the image reaches across the three, so they are three C
 * objects; the offsets above are the only coupling.
 */
extern cal_$timezone_rec_t CAL_$TIMEZONE;   /* 0x00E7B030 */
extern uint CAL_$LAST_VALID_TIME;           /* 0x00E7B03C */
extern int16_t CAL_$BOOT_VOLX;              /* 0x00E7B040 */

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
 *     8   D10   Date tens (0-3)      D2 = leap year, D3 unused (always 0)
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
#define CAL_$RTC_BASE               SAU2_CALENDAR_BASE  /* 0x00FFA800, long at 0x00E2AF66 */
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
#define MSM5832_D10_LEAP_FLAG 0x04 /* D10 bit 2: leap year             */
#define MSM5832_D10_UNUSED    0x08 /* D10 bit 3: not connected in the chip */
#define MSM5832_H10_24H_FLAG  0x08 /* H10 bit 3: 24-hour select        */
#define MSM5832_TENS_MASK     0x03 /* D0-D1 of a flag-carrying tens reg */

/*
 * Resolved: CAL_$WRITE_CALENDAR has an original bug in the leap-year flag
 * (bead source-tcxm).
 *
 * Both sides compute the same leap predicate -- (year_2digit + (month > 2))
 * mod 4 == 0 -- but they deposit the result in different bits of D10:
 *
 *   TIME_$READ_CAL   0x00E2AE78  bset.l #2,D1     -> D10 bit 2  (correct)
 *                    0x00E2AEC8  and.w #3,D1      -> date tens is D0-D1 only
 *   CAL_$WRITE_CALENDAR
 *                    0x00E816D6  add.w #0x50,D1w  -> (day + 80) / 10 is
 *                                (day / 10) + 8, i.e. D10 bit 3
 *
 * The MSM5832 defines D10 as {D0,D1 = date tens, D2 = leap year, D3 = unused},
 * so the writer sets a bit the chip ignores and leaves the leap-year flag
 * clear.  The same `add.w #0x50` idiom one register later (0x00E816EA, on the
 * hour) IS correct, because H10 bit 3 is the 24-hour select -- the writer
 * evidently reused it on the wrong register.  The correct constant for the
 * date would have been 0x28 (add 40 == +4 in the tens digit).
 *
 * Observable effect: the chip's internal leap counter is never programmed by
 * CAL_$WRITE_CALENDAR, so an MSM5832 left to run would skip February 29.  The
 * date read back is unaffected, because the reader masks D10 with 3 and the
 * chip ignores D3; and TIME_$READ_CAL re-writes D10 with bit 2 set on every
 * read taken during a leap year, which repairs the flag at the next boot.
 *
 * Both sides are reproduced exactly as found.
 */

/*
 * Register access, isolated behind the arch layer: ARCH_IO_WRITE8 /
 * ARCH_IO_READ8 (arch/arch.h) at the SAU2 calendar address.  On the target
 * these are plain volatile MMIO byte accesses; on the host build (unit
 * tests) they route through hooks the test supplies, so a test can observe
 * the exact control/data sequence the driver emits.
 */
#define CAL_$RTC_WRITE(off, val) ARCH_IO_WRITE8(CAL_$RTC_BASE + (off), (val))
#define CAL_$RTC_READ(off)       ARCH_IO_READ8(CAL_$RTC_BASE + (off))

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
/*
 * CAL_$VERIFY (0x00E68380): max_delta by reference, msg_arg passed through
 * to the "More than %a days" VFMT call, interactive a Domain boolean by
 * reference, result a Domain boolean (0xFF / 0) - see cal/verify.c.
 */
extern char CAL_$VERIFY(int *max_delta, void *msg_arg, char *interactive,
                        status_$t *status_ret);
extern void CAL_$WRITE_CALENDAR(int16_t *year, int16_t *month, int16_t *day,
                                int16_t *weekday, int16_t *hour,
                                int16_t *minute, int16_t *second);
extern void CAL_$SETUP_CALLBACK(void *callback_queue);

#endif /* CAL_H */
