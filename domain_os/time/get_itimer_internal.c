/*
 * time_$get_itimer_internal - read a process's interval timer
 *
 * Reads TIME_$ITIMER_DB[which][PROC1_$AS_ID] and returns the reload interval
 * plus the time REMAINING until the next expiry.  The database holds the
 * absolute expiry, so the remaining time is that expiry minus the current
 * clock; a deadline already in the past comes back as zero.
 *
 * Original address: 0x00e58c74 (160 bytes)
 *
 * Assembly:
 *   00e58c74  link.w A6,-0x10
 *   00e58c7c  move.w (0x8,A6),D0w        ; which
 *   00e58c80  movea.l (0xa,A6),A0        ; interval (out)
 *   00e58c84  movea.l (0xe,A6),A2        ; value    (out)
 *   00e58c8a..00e58ca8                   ; A1 = &TIME_$ITIMER_DB[which][as_id]
 *   00e58cac  btst.b #0x0,(0x13,A1)      ; elem->flags & 1 (in use)
 *   00e58cb2  beq -> 0x00e58cfe          ; not armed: return two zeroes
 *   00e58cb4  move.l (0x14,A1),(A0)      ; *interval = elem->interval
 *   00e58cb8  move.w (0x18,A1),(0x4,A0)
 *   00e58cbe  move.l (0xc,A1),(A2)       ; *value = elem->expire
 *   00e58cc2  move.w (0x10,A1),(0x4,A2)
 *   00e58cc8  tst.w D0w / bne 0x00e58cd8
 *   00e58ccc  pea (-0xc,A6) / jsr TIME_$ABS_CLOCK      ; which == 0
 *   00e58cd8  pea (-0xc,A6) / jsr PROC1_$GET_CPUT8     ; which != 0
 *   00e58ce4  pea (-0xc,A6) / pea (A2) / jsr SUB48     ; *value -= now
 *   00e58cf2  tst.b D0b / bmi -> return
 *   00e58cf6  clr.l (A2) / clr.w (0x4,A2)
 *   00e58cfe  clr.l (A2) / clr.w (0x4,A2) / clr.l (A0) / clr.w (0x4,A0)
 *
 * SUB48 (0x00E172E4) ends in "spl D0b", so it returns 0xFF (-1 as int8_t)
 * when the difference is NON-NEGATIVE.  "bmi" therefore keeps the remaining
 * time when the deadline is still ahead, and the fall-through (D0 == 0, the
 * difference went negative) zeroes it.  This is the same convention
 * time/set_cpu_limit.c already documents at 0xE59036.
 *
 * The callers reserve a two-byte Pascal result slot above the arguments
 * (0x00E58D92 and 0x00E58F1A "subq.l #0x2,SP", both followed by
 * "lea (0xc,SP),SP") but neither writes nor reads it, so nothing is returned.
 */

#include "time/time_internal.h"

void time_$get_itimer_internal(uint16_t which, clock_t *interval,
                               clock_t *value)
{
    time_queue_elem_t *entry;
    clock_t now;

    /* 0xE58C8A..0xE58CA8 */
    entry = time_$itimer_entry(which, PROC1_$AS_ID);

    /* 0xE58CAC: bit 0 of the flags word */
    if ((entry->flags & ITIMER_FLAG_IN_USE) == 0) {
        /* 0xE58CFE: both buffers are cleared, value first */
        value->high = 0;
        value->low = 0;
        interval->high = 0;
        interval->low = 0;
        return;
    }

    /* 0xE58CB4 */
    interval->high = entry->interval_high;
    interval->low = entry->interval_low;

    /* 0xE58CBE: the database holds the ABSOLUTE expiry */
    value->high = entry->expire_high;
    value->low = entry->expire_low;

    /* 0xE58CC8: the real timer runs on wall clock, the virtual one on CPU time */
    if (which == 0) {
        TIME_$ABS_CLOCK(&now);
    } else {
        PROC1_$GET_CPUT8(&now);
    }

    /* 0xE58CEA: *value = expiry - now; a negative result means it has fired */
    if (SUB48(value, &now) >= 0) {
        /* 0xE58CF6 */
        value->high = 0;
        value->low = 0;
    }
}
