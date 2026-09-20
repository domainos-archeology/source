/*
 * PROC1_$GET_CPUT / PROC1_$GET_CPUT8 - CPU time of the current process
 * Original addresses: 0x00e20894 (20 bytes), 0x00e2089c (2 bytes)
 *
 * Both are entries into one 26-byte block of hand-written code that ends
 * at the shared store at 0x00E2089E; proc1_$get_current_cpu_time
 * (0x00E208D0, no map symbol) is the register-convention helper all three
 * CPU-time readers `bsr':
 *
 * 0x00E208D0  move SR,-(SP) / ori #0x700,SR       true save + raise
 * 0x00E208D6  jsr TIME_$VT_TIMER                  D0.w = timer now
 * 0x00E208DC  movea.l (-0x1e16,PC),A1             A1 = PROC1_$CURRENT_PCB
 * 0x00E208E0  neg.w D0 / add.w (0x48,A1),D0       D0 = vtimer - now
 * 0x00E208E6  D1 = (0x4C,A1)                      cpu_total
 * 0x00E208EA  add.w (0x50,A1),D0 / bcc / addq.l #1,D1   D0 += cpu_usage,
 *                                                  carry into D1
 * 0x00E208F2  move (SP)+,SR / rts                 restore
 *
 * PROC1_$GET_CPUT (0x00E20894):
 * 0x00E20894  bsr.b 0x00E208D0
 * 0x00E20896  lsl.w #1,D0 / roxl.l #1,D1          48-bit shift left by one:
 *                                                  bit 15 of D0 rotates into
 *                                                  bit 0 of D1
 * 0x00E2089A  bra.b 0x00E2089E
 *
 * PROC1_$GET_CPUT8 (0x00E2089C):
 * 0x00E2089C  bsr.b 0x00E208D0                    then falls into the store
 *
 * 0x00E2089E  movea.l (0x4,SP),A0                 A0 = clock (argument 1)
 * 0x00E208A2  move.l D1,(A0) / move.w D0,(0x4,A0)  clock->high, clock->low
 * 0x00E208A8  rts
 *
 * GET_CPUT is reached through the SVC table (0x00E7B36E) and STOP_$WATCH
 * (0x00E81B54); GET_CPUT8 from PROC2 (0x00E58CDC, 0x00E58D72, 0x00E58FB6).
 */

#include "proc1/proc1_internal.h"
#include "time/time.h"

/*
 * proc1_$get_current_cpu_time (0x00E208D0): high = cpu_total (+1 on carry),
 * low = cpu_usage + (vtimer - TIME_$VT_TIMER()), all read under IPL 7.
 */
void proc1_$get_current_cpu_time(uint32_t *high_out, uint16_t *low_out)
{
    proc1_t *pcb;
    uint16_t now;               /* D0.w after the jsr */
    uint16_t d0;
    uint32_t d1;
    uint16_t saved_sr;
    uint32_t sum;

    /* 0x00E208D0 / 0x00E208D2: move SR,-(SP) / ori #0x700,SR */
    DISABLE_INTERRUPTS(saved_sr);

    /* 0x00E208D6 */
    now = (uint16_t)TIME_$VT_TIMER();

    /* 0x00E208DC */
    pcb = PROC1_$CURRENT_PCB;

    /* 0x00E208E0 / 0x00E208E2: D0 = -now + vtimer */
    d0 = (uint16_t)((uint16_t)pcb->vtimer - now);

    /* 0x00E208E6 */
    d1 = pcb->cpu_total;

    /* 0x00E208EA..0x00E208F0: D0 += cpu_usage, carry -> D1 */
    sum = (uint32_t)d0 + (uint32_t)pcb->cpu_usage;
    d0 = (uint16_t)sum;
    if (sum > 0xFFFFu) {
        d1++;
    }

    /* 0x00E208F2: move (SP)+,SR */
    ENABLE_INTERRUPTS(saved_sr);

    *high_out = d1;
    *low_out = d0;
}

void PROC1_$GET_CPUT(clock_t *clock)
{
    uint32_t d1;
    uint16_t d0;

    /* 0x00E20894 */
    proc1_$get_current_cpu_time(&d1, &d0);

    /* 0x00E20896: lsl.w #1,D0 (X = old bit 15) / roxl.l #1,D1 */
    d1 = (d1 << 1) | ((uint32_t)d0 >> 15);
    d0 = (uint16_t)(d0 << 1);

    /* 0x00E2089E..0x00E208A4 */
    clock->high = d1;
    clock->low = d0;
}

void PROC1_$GET_CPUT8(clock_t *clock)
{
    uint32_t d1;
    uint16_t d0;

    /* 0x00E2089C */
    proc1_$get_current_cpu_time(&d1, &d0);

    /* 0x00E2089E..0x00E208A4 */
    clock->high = d1;
    clock->low = d0;
}
