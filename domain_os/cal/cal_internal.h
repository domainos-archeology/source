/*
 * cal/cal_internal.h - Calendar subsystem internal API
 *
 * Internal declarations shared only by the CAL subsystem's implementation
 * files.  Every non-test .c file in cal/ includes this header first.
 */

#ifndef CAL_INTERNAL_H
#define CAL_INTERNAL_H

#include "cal/cal.h"
#include "bat/bat.h"

/*
 * PROC1 lock ID used to serialize access to the boot volume label block
 * (CAL_$READ_TIMEZONE / CAL_$WRITE_TIMEZONE): `move.w #0xe,-(SP)` at
 * 0x00E3E548 / 0x00E3E57E / 0x00E3E5C0 and 0x00E3E642 / 0x00E3E678 /
 * 0x00E3E6C2.
 */
#define CAL_LOCK_ID 0xe

/*
 * cal_$label_tz_t - the timezone fields of the LV label (block 0 of the
 * boot volume, bat_$label_t).
 *
 * CAL_$READ_TIMEZONE reads and CAL_$WRITE_TIMEZONE writes three fields at
 * fixed label offsets that bat/bat.h still carries as reserved_d0[0x2c]:
 *
 *   0xE0  utc_delta        word      (0x00E3E58E move.w (0xe0,A4),(A5))
 *   0xE2  tz_name[4]       4 bytes   (0x00E3E592..0x00E3E5A0, byte copies)
 *   0xE6  last_valid_time  longword  (0x00E3E5A2 move.l (0xe6,A4),(0xc,A5))
 *
 * The view is packed because 0xE6 is not longword-aligned.  Naming these
 * inside bat_$label_t itself is bat's call (bead source-xb3b).
 */
typedef struct __attribute__((packed, aligned(2))) cal_$label_tz_t {
    int16_t  utc_delta;         /* label 0xE0 */
    char     tz_name[4];        /* label 0xE2 */
    uint32_t last_valid_time;   /* label 0xE6 */
} cal_$label_tz_t;

#define CAL_LABEL_TZ_OFFSET 0xE0

_Static_assert(sizeof(cal_$label_tz_t) == 10, "cal_$label_tz_t: 0xE0..0xE9");
_Static_assert(__builtin_offsetof(cal_$label_tz_t, last_valid_time) == 6,
               "cal_$label_tz_t.last_valid_time is label 0xE6");
_Static_assert(__builtin_offsetof(bat_$label_t, reserved_d0) <= CAL_LABEL_TZ_OFFSET &&
               CAL_LABEL_TZ_OFFSET + sizeof(cal_$label_tz_t) <=
               __builtin_offsetof(bat_$label_t, reserved_d0) + sizeof(((bat_$label_t *)0)->reserved_d0),
               "cal_$label_tz_t must sit inside bat_$label_t.reserved_d0");

/* The timezone view of a label buffer returned by DBUF_$GET_BLOCK. */
#define CAL_$LABEL_TZ(label) \
    ((cal_$label_tz_t *)((uint8_t *)(label) + CAL_LABEL_TZ_OFFSET))

#endif /* CAL_INTERNAL_H */
