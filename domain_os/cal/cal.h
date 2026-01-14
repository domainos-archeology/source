#include "base/base.h"
#include "math/math.h"

typedef long status_$t;

#define status_$ok 0
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
#define CAL_$BOOT_VOLX CAL_$TIMEZONE.boot_volx

// Last valid time (high word of clock) at 0x00e7b03c
extern uint CAL_$LAST_VALID_TIME;

// Hardware clock registers
extern volatile char CAL_$CONTROL_VIRTUAL_ADDR;    // 0x00ffa820
extern volatile char CAL_$WRITE_DATA_VIRTUAL_ADDR; // 0x00ffa822

// External references
extern char NETWORK_$DISKLESS;
extern char NETWORK_$REALLY_DISKLESS;

// 48-bit arithmetic
extern void ADD48(clock_t *dst, clock_t *src);
extern void SUB48(clock_t *dst, clock_t *src);

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
extern void CAL_$WRITE_CALENDAR(short *year, short *month, short *day,
                                short *weekday, short *hour, short *minute,
                                short *second);
