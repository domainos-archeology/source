/*
 * TIME_$SET_CPU_LIMIT - Set CPU time limit
 *
 * Installs (or removes) a per-address-space CPU time limit.  When the limit
 * is reached the process's virtual timer queue fires
 * TIME_$SET_CPU_LIMIT_CALLBACK, which signals it.
 *
 * Parameters:
 *   limit    - Pointer to the limit in ITIMER form (48 bits, two ticks per
 *              unit); converted with time_$itimer_to_clock before use
 *   relative - Pointer to a Domain boolean: true (0xFF) = relative to the
 *              process's current CPU time, false = absolute
 *   status   - Status return
 *
 * Original address: 0x00e58f64
 *
 * Assembly:
 *   00e58f78  clr.l (A3)                    ; *status = status_$ok
 *   00e58f7a  move.l #0xe58af8,(-0x10,A6)   ; the callback, taken via a local
 *   00e58f82..00e58f96                      ; A4 = &TIME_$VTQ[PROC1_$CURRENT-1]
 *   00e58fa0  bsr time_$itimer_to_clock(&tmp, limit)
 *   00e58fa6  move.l (-0x30,A6),(-0x8,A6)   ; limit_clock = tmp
 *   00e58fac  move.w (-0x2c,A6),(-0x4,A6)
 *   00e58fb6  jsr PROC1_$GET_CPUT8(&cpu_clock)
 *   00e58fbe  clr.l (-0x18,A6) / clr.w (-0x14,A6)   ; interval = 0
 *   00e58fc8..00e58fde                      ; cpu_entry = 0xE29198 + as_id*0x1C
 *   00e58fe4  jsr TIME_$Q_REMOVE_ELEM(vt_queue, cpu_entry, status)
 *   00e58fee  tst.l (A2) / bne  ) tests the RAW limit, not the halved copy
 *   00e58ff2  tst.w (0x4,A2) / bne )
 *   00e5900e  move.l (A2),(0xc,A0,D0w) / move.w (0x4,A2),(0x10,A0,D0w)
 *   00e59018  clr.l (A3)                    ; *status = status_$ok
 *   00e5901e  move.l (A2),(-0x20,A6)        ; scratch = *limit (raw)
 *   00e59022  move.w (0x4,A2),(-0x1c,A6)
 *   00e5902a  tst.b (A0) / bmi 0x00e59068   ; relative -> schedule
 *   00e59036  jsr SUB48(&scratch, &cpu_clock)   ; scratch is destroyed here
 *   00e5903e  tst.b D0b / bmi 0x00e59068    ; limit still in the future
 *   00e59042..00e5905e  PROC2_$SIGNAL_OS, then return
 *   00e5906c  bpl 0x00e590a8                ; is_absolute = relative ? 0 : 1
 *   00e590e8  jsr TIME_$Q_ADD_CALLBACK
 */

#include "time/time_internal.h"

void TIME_$SET_CPU_LIMIT(clock_t *limit, boolean *relative, status_$t *status)
{
    clock_t converted;
    clock_t cpu_clock;
    clock_t scratch;
    clock_t interval;
    clock_t limit_clock;
    void *callback;
    time_queue_t *vt_queue;
    int16_t as_offset;
    time_queue_elem_t *cpu_entry;

    /* 0xE58F78 */
    *status = status_$ok;

    /* 0xE58F7A: the callback address is materialised into a frame slot */
    callback = (void *)TIME_$SET_CPU_LIMIT_CALLBACK;

    /* 0xE58F82..0xE58F96: lea (-0xc,A0,D0w) with A0 = 0xE2A4A0, D0 = cur*12 */
    vt_queue = &TIME_$VTQ[PROC1_$CURRENT - 1];

    /* 0xE58FA0: halve the itimer-form limit into limit_clock */
    time_$itimer_to_clock(&converted, limit);
    limit_clock.high = converted.high;
    limit_clock.low = converted.low;

    /* 0xE58FB6 */
    PROC1_$GET_CPUT8(&cpu_clock);

    /* 0xE58FBE: one-shot */
    interval.high = 0;
    interval.low = 0;

    /* 0xE58FC8..0xE58FDE: as_id*32 - as_id*4 == as_id*0x1C */
    as_offset = (int16_t)(PROC1_$AS_ID * CPU_LIMIT_DB_ENTRY_SIZE);
    cpu_entry = (time_queue_elem_t *)ARCH_VA_TO_PTR(CPU_LIMIT_DB_BASE +
                                                    as_offset);

    /* 0xE58FE4: drop any limit already armed */
    TIME_$Q_REMOVE_ELEM(vt_queue, cpu_entry, status);

    /* 0xE58FEE: the RAW limit is what is tested for zero */
    if (limit->high == 0 && limit->low == 0) {
        /* 0xE5900E: stores the (zero) limit words back into the entry */
        cpu_entry->expire_high = limit->high;
        cpu_entry->expire_low = limit->low;
        *status = status_$ok;
        return;
    }

    /* 0xE5901E: a scratch copy of the raw limit, destroyed by SUB48 below */
    scratch.high = limit->high;
    scratch.low = limit->low;

    /* 0xE5902A: Domain boolean, true is 0xFF */
    if (*relative >= 0) {
        /*
         * 0xE59036: SUB48 returns true (-1) when scratch - cpu_clock is
         * non-negative, i.e. the absolute limit has NOT been reached yet.
         */
        if (SUB48(&scratch, &cpu_clock) >= 0) {
            /* 0xE59042: limit already exceeded - signal immediately */
            PROC2_$SIGNAL_OS(&PROC2_$UID[PROC1_$AS_ID],
                             (int16_t *)&time_$c_cpu_limit_signal,
                             (uint32_t *)&time_$c_cpu_limit_fault,
                             status);
            return;
        }
    }

    /*
     * 0xE5906C: both arms of the join push the same ten arguments; only the
     * is_absolute word differs (0 for a relative limit, 1 for an absolute one).
     */
    TIME_$Q_ADD_CALLBACK(vt_queue,
                         &limit_clock,
                         (*relative < 0) ? 0 : 1,
                         &cpu_clock,
                         callback,
                         (void *)(uintptr_t)PROC1_$AS_ID,
                         4,
                         &interval,
                         cpu_entry,
                         status);
}
