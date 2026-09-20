/*
 * PROC1_$SET_PRIORITY - Set or query a process's priority range
 * Original address: 0x00e1523c (164 bytes; Ghidra's 168 includes the
 *                   trailing status cell at 0x00E152E0)
 *
 * Frame: (0x8,A6) pid (word), (0xA,A6) set (boolean BYTE, `move.b'),
 * (0xC,A6) min, (0x10,A6) max.
 *
 * 0x00E1523C  link.w A6,-0x4 / movem.l D2/D3/A2-A4,-(SP)
 * 0x00E15244  D2 = pid; D3 = set; A2 = min; A3 = max
 * 0x00E15254  tst.w D2 / beq crash; cmpi.w #0x40 / bls ok
 * 0x00E1525E  CRASH_SYSTEM(&Illegal_process_id_err)  (`pea (0x80,PC)' ->
 *             0x00E152E0; addq #4; FALLS THROUGH afterwards)
 * 0x00E1526A  A4 = PCBS[pid]
 * 0x00E15278  tst.b D3 / bpl 0x00E152CE                query
 * 0x00E1527C  (0x56,A4) = clamp(*min)                  inh_count  (min)
 * 0x00E15288  D0 = (0x58,A4) = clamp(*max)             sw_bsr     (max)
 * 0x00E15294  ori #0x700,SR                            raise, no save
 * 0x00E15298  cmp.w (0x52,A4),D0 / blt 0x00E152A8      max < state: clamp
 * 0x00E1529E  D0 = (0x56,A4); cmp.w (0x52,A4),D0 / ble 0x00E152AC
 *                                                      min <= state: keep
 * 0x00E152A8  (0x52,A4) = D0                           state = max or min
 * 0x00E152AC  D0 = word (0x54,A4) & 0xB; cmpi.w #8 / bne 0x00E152C8
 * 0x00E152B8  PROC1_$REORDER_READY(pcb) (`pea (A4)'; addq #4)
 * 0x00E152C2  PROC1_$DISPATCH()
 * 0x00E152C8  andi #-0x701,SR                          forced IPL 0
 * 0x00E152CC  bra 0x00E152D6
 * 0x00E152CE  *min = (0x56,A4); *max = (0x58,A4)
 * 0x00E152D6  movem.l / unlk / rts
 *
 * proc1_$clamp_priority (0x00E15222, `bsr.b', Pascal function):
 *   D0 = value; if value <= 1 -> 1; else if value >= 0x10 -> 0x10; else
 *   value (both compares unsigned: `bls' / `bcs').
 *
 * The BOUND|SUSPENDED|WAITING test (0xB) against BOUND alone decides
 * whether the PCB is on the ready list and so worth re-ordering.  Note the
 * clamp of state is taken only against max first, then min, using the
 * freshly stored values.
 *
 * Parameters:
 *   pid          - the process (1..0x40; anything else crashes the system)
 *   set          - Domain boolean: true = store min/max, false = read them
 *   min_priority - the minimum state (inh_count)
 *   max_priority - the maximum state (sw_bsr)
 */

#include "proc1/proc1_internal.h"
#include "misc/misc.h"

/* 0x00E15222: clamp to 1..0x10 with unsigned compares */
static uint16_t proc1_$clamp_priority(uint16_t value)
{
    uint16_t d1;

    /* 0x00E1522A / 0x00E1522C: moveq #1 / cmp.w / bls -> 1 */
    d1 = 1;
    if (value <= d1) {
        return d1;
    }
    /* 0x00E15230 / 0x00E15232: moveq #0x10 / cmp.w / bcs -> value */
    d1 = 0x10;
    if (value < d1) {
        return value;
    }
    /* 0x00E15236 */
    return d1;
}

void PROC1_$SET_PRIORITY(uint16_t pid, int8_t set, uint16_t *min_priority,
                         uint16_t *max_priority)
{
    proc1_t *pcb;               /* A4 */
    uint16_t d0;
    uint16_t flags;

    /* 0x00E15254 / 0x00E15258 */
    if (pid == 0 || pid > 0x40) {
        /* 0x00E1525E: no return after the crash call in the image */
        CRASH_SYSTEM(&Illegal_process_id_err);
    }

    /* 0x00E1526A..0x00E15274 */
    pcb = PCBS[pid];

    /* 0x00E15278: tst.b D3b / bpl */
    if (set < 0) {
        /* 0x00E1527C..0x00E15284 */
        pcb->inh_count = proc1_$clamp_priority(*min_priority);

        /* 0x00E15288..0x00E15290 */
        d0 = proc1_$clamp_priority(*max_priority);
        pcb->sw_bsr = d0;

        /* 0x00E15294: ori #0x700,SR */
        SET_IPL7();

        /* 0x00E15298: cmp.w (0x52,A4),D0w / blt (signed) */
        if ((int16_t)d0 < (int16_t)pcb->state) {
            /* 0x00E152A8 */
            pcb->state = d0;
        } else {
            /* 0x00E1529E..0x00E152A6 */
            d0 = pcb->inh_count;
            if ((int16_t)d0 > (int16_t)pcb->state) {
                /* 0x00E152A8 */
                pcb->state = d0;
            }
        }

        /* 0x00E152AC..0x00E152B6 */
        flags = (uint16_t)(((uint16_t)pcb->pri_min << 8) | pcb->pri_max);
        if ((flags & 0x000B) == 0x0008) {
            /* 0x00E152B8 / 0x00E152C2 */
            PROC1_$REORDER_READY(pcb);
            PROC1_$DISPATCH();
        }

        /* 0x00E152C8: andi #-0x701,SR */
        SET_IPL0();
    } else {
        /* 0x00E152CE / 0x00E152D2 */
        *min_priority = pcb->inh_count;
        *max_priority = pcb->sw_bsr;
    }
}
