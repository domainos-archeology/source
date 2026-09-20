/*
 * PROC1_$EC_WAITN - Wait for any of N level-1 eventcounts
 * Original address: 0x00e2065a (120 bytes)
 *
 * Re-emitted from the disassembly.  In the image this is the hand-written
 * body local to EC_$WAITN (SAU2 map: EC_$WAITN 0xE2063E, the next symbol is
 * PROC1_$REMOVE_READY 0xE206D2): it takes its arguments in registers -
 * A1 = pcb, A4 = &ecs[0], A3 = &vals[0], D0.w = count - and is reached with
 * `bsr' from EC_$WAIT (0x00E20632), EC_$WAITN (0x00E20652), ML_$LOCK
 * (0x00E20B4A) and ML_$EXCLUSION_START (0x00E20E28).  The tree gives it the
 * C signature below (ec/wait.c, ec/waitn.c, ml/exclusion_start.c call it
 * that way).  The waiter cells the image builds on its own stack become a
 * local array here; their layout is ec_$eventcount_waiter_t (ec/ec.h) and
 * every link is written exactly as the image writes it.
 *
 * 0x00E2065A  move.l D2,-(SP) / move.l A2,-(SP)
 * 0x00E2065E  D2 = 0                                   cells pushed so far
 * 0x00E20660  ori #0x700,SR                            raise, no SR saved
 * 0x00E20664  subq.w #1,D0 / blt 0x00E206C6            count <= 0: exit
 * 0x00E20668  D2++
 * 0x00E2066A  A0 = *A4++ (ec); D1 = *A3++ (value wanted)
 * 0x00E2066E  A2 = (0x4,A0)                            ec's current head
 * 0x00E20672  push A1, A0, A2, D1                      the 16-byte cell:
 *               (0x0,SP) value, (0x4,SP) prev = A2, (0x8,SP) next = ec,
 *               (0xC,SP) pcb
 * 0x00E2067A  (0x8,A2) = SP                            old head's next = cell
 * 0x00E2067E  (0x4,A0) = SP                            ec's head = cell
 * 0x00E20682  D1 -= (A0); tst.l D1
 * 0x00E20686  dble D0,0x00E20668                       D1 > 0: D0--, loop
 *                                                      while D0 != -1
 * 0x00E2068A  ble 0x00E206A0                           D1 <= 0: satisfied
 * 0x00E2068C  bsr proc1_$remove_from_ready_list (A1)   (0x00E206D6)
 * 0x00E2068E  bset.b #0,(0x55,A1)                      PROC1_FLAG_WAITING
 * 0x00E20694  (0x3C,A1) = TIME_$CLOCKH                 (0x00E2B0D4)
 * 0x00E2069C  bsr PROC1_$DISPATCH_INT2                 (0x00E20A24)
 * 0x00E206A0  D0 = -1; D2--                            teardown
 * 0x00E206A4  pop D1, A2, A0, A1                       the newest cell
 * 0x00E206AC  (0x4,A0) = A2                            ec's head = prev
 * 0x00E206B0  (0x8,A2) = A0                            prev's next = ec
 * 0x00E206B4  A2 = *--A4; D1 -= (A2); tst.l D1
 * 0x00E206BA  dble D2,0x00E206A4                       D1 > 0: D2--, loop
 * 0x00E206BE  bgt 0x00E206C6                           (loop ran out) exit
 * 0x00E206C0  D0 = D2                                  satisfied: remember
 * 0x00E206C2  dbf D2,0x00E206A4                        keep popping
 * 0x00E206C6  andi #-0x701,SR                          forced IPL 0
 * 0x00E206CA  addq.w #1,D0
 * 0x00E206CC  movea.l (SP)+,A2 / move.l (SP)+,D2 / rts
 *
 * Behaviour:
 *   - the cells are linked one at a time and the scan stops at the first
 *     eventcount whose value already reached what was asked (D1 <= 0);
 *     only if every one of them is still short does the process leave the
 *     ready list, mark itself WAITING, stamp wait_start and dispatch;
 *   - the teardown always unlinks every cell that was linked (newest
 *     first); D0 is overwritten with the index of each satisfied
 *     eventcount as it is met, so the value returned is the LOWEST
 *     satisfied index plus one, or 0 when none is satisfied;
 *   - count <= 0 returns count itself (D0 = count - 1, then + 1);
 *   - the IPL is raised at entry and forced to 0 at exit, whatever it was.
 *
 * Parameters:
 *   pcb   - the waiting process (A1)
 *   ecs   - the eventcounts, ecs[0..count-1] (A4)
 *   vals  - the values waited for, one per eventcount (A3)
 *   count - number of eventcounts (D0.w)
 *
 * Returns:
 *   1-based index of the lowest satisfied eventcount, 0 if none (D0.w)
 */

#include "proc1/proc1_internal.h"
#include "time/time.h"

/*
 * The cell the image pushes (0x00E20672..0x00E20678) is
 * ec_$eventcount_waiter_t, and the eventcount's (0x4)/(0x8) links overlay a
 * cell's prev/next so the head of an empty list can point at the eventcount
 * itself (EC_$INIT 0x00E151FE sets both links to the eventcount).
 */
#if defined(ARCH_M68K)  /* the record carries pointers: target layout only */
_Static_assert(__builtin_offsetof(ec_$eventcount_waiter_t, wait_val) == 0x0,
               "waiter value at (0x0,SP)");
_Static_assert(__builtin_offsetof(ec_$eventcount_waiter_t, prev_waiter) == 0x4,
               "waiter prev at (0x4,SP)");
_Static_assert(__builtin_offsetof(ec_$eventcount_waiter_t, next_waiter) == 0x8,
               "waiter next at (0x8,SP)");
_Static_assert(__builtin_offsetof(ec_$eventcount_waiter_t, pcb) == 0xC,
               "waiter pcb at (0xC,SP)");
_Static_assert(__builtin_offsetof(ec_$eventcount_t, waiter_list_head) ==
               __builtin_offsetof(ec_$eventcount_waiter_t, prev_waiter),
               "ec (0x4) overlays waiter prev");
_Static_assert(__builtin_offsetof(ec_$eventcount_t, waiter_list_tail) ==
               __builtin_offsetof(ec_$eventcount_waiter_t, next_waiter),
               "ec (0x8) overlays waiter next");
#endif

uint16_t PROC1_$EC_WAITN(proc1_t *pcb, ec_$eventcount_t **ecs,
                         int32_t *vals, int16_t count)
{
    int16_t d0;                         /* D0.w */
    int16_t d2;                         /* D2.w: cells linked */
    int32_t d1;                         /* D1 */
    ec_$eventcount_t *ec;               /* A0 */
    ec_$eventcount_waiter_t *prev;      /* A2 */
    ec_$eventcount_waiter_t *cell;

    /* 0x00E2065E */
    d2 = 0;

    /* 0x00E20660: ori #0x700,SR - nothing is saved */
    SET_IPL7();

    /* 0x00E20664: subq.w #1,D0 / blt 0x00E206C6 */
    d0 = (int16_t)(count - 1);
    if (d0 < 0) {
        goto exit;
    }

    {
        /* the image pushes one 16-byte cell per eventcount onto its stack */
        ec_$eventcount_waiter_t cells[count];

        /* 0x00E20668..0x00E20686: link a cell for each eventcount */
        for (;;) {
            /* 0x00E20668 */
            d2++;
            cell = &cells[d2 - 1];

            /* 0x00E2066A / 0x00E2066C */
            ec = ecs[d2 - 1];
            d1 = vals[d2 - 1];

            /* 0x00E2066E: A2 = (0x4,A0) */
            prev = ec->waiter_list_head;

            /* 0x00E20672..0x00E20678: the pushed cell */
            cell->wait_val = d1;
            cell->prev_waiter = prev;
            cell->next_waiter = (ec_$eventcount_waiter_t *)ec;
            cell->pcb = pcb;

            /* 0x00E2067A: (0x8,A2) = SP */
            prev->next_waiter = cell;

            /* 0x00E2067E: (0x4,A0) = SP */
            ec->waiter_list_head = cell;

            /* 0x00E20682: sub.l (A0),D1 / tst.l D1 */
            d1 -= ec->value;

            /* 0x00E20686: dble D0w - D1 <= 0 falls through to the ble */
            if (d1 <= 0) {
                /* 0x00E2068A: ble 0x00E206A0 */
                goto teardown;
            }
            d0--;
            if (d0 == -1) {
                break;
            }
        }

        /* 0x00E2068C: nothing satisfied - block */
        proc1_$remove_from_ready_list(pcb);

        /* 0x00E2068E: bset.b #0x0,(0x55,A1) */
        pcb->pri_max = (uint8_t)(pcb->pri_max | PROC1_FLAG_WAITING);

        /* 0x00E20694 */
        pcb->wait_start = TIME_$CLOCKH;

        /* 0x00E2069C */
        PROC1_$DISPATCH_INT2(pcb);

teardown:
        /* 0x00E206A0 / 0x00E206A2 */
        d0 = -1;
        d2--;

        /* 0x00E206A4..0x00E206C2: pop every cell, newest first */
        for (;;) {
            cell = &cells[d2];

            /* 0x00E206A4..0x00E206AA: pop D1, A2, A0, A1 */
            d1 = cell->wait_val;
            prev = cell->prev_waiter;
            ec = (ec_$eventcount_t *)cell->next_waiter;
            pcb = (proc1_t *)cell->pcb;

            /* 0x00E206AC: (0x4,A0) = A2 */
            ec->waiter_list_head = prev;

            /* 0x00E206B0: (0x8,A2) = A0 */
            prev->next_waiter = (ec_$eventcount_waiter_t *)ec;

            /* 0x00E206B4: A2 = *--A4 / sub.l (A2),D1 / tst.l D1 */
            d1 -= ecs[d2]->value;

            /* 0x00E206BA: dble D2w,0x00E206A4 */
            if (d1 > 0) {
                d2--;
                if (d2 != -1) {
                    continue;
                }
                /* 0x00E206BE: bgt 0x00E206C6 - D1 is still > 0 */
                break;
            }

            /* 0x00E206BE not taken; 0x00E206C0: D0 = D2 */
            d0 = d2;

            /* 0x00E206C2: dbf D2w,0x00E206A4 */
            d2--;
            if (d2 == -1) {
                break;
            }
        }
    }

exit:
    /* 0x00E206C6: andi #-0x701,SR - forced IPL 0 */
    SET_IPL0();

    /* 0x00E206CA */
    d0++;
    return (uint16_t)d0;
}
