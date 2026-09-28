/*
 * time_$q_setup_timer - Program the hardware timer for a queue's head element
 *
 * Third unnamed procedure of the TIME_Q_ module.  Called with the queue lock
 * held from TIME_$Q_REENTER_ELEM (0x00E16D40), TIME_$Q_ENTER_ELEM
 * (0x00E16DB4) and TIME_$Q_SCAN_QUEUE (0x00E16FBE).
 *
 * Frame (0x00E16BE0): 0x08 queue (A2), 0x0C now.  Locals: -0x10 delay
 * (clock_t), -0x08 status (PROC1_$SET_VT's), -0x16 tick word.
 *
 *   00e16be4  head = queue->head; delay = head->expire
 *   00e16bfa  SUB48(&delay, now); tst.b D0b / bmi     ; negative result ->
 *   00e16c06  clr.l (-0x10,A6) / clr.w (-0xc,A6)      ;   delay = 0
 *   00e16c0e  tst.b (0x8,A2) / bpl                    ; VT queue?
 *   00e16c14  subq.l #2,SP; PROC1_$SET_VT(queue_id, &delay, &status)
 *   00e16c2a  move.l (-0x10,A6),D0 / lsr.l #5 / beq   ; high >> 5 != 0 ->
 *   00e16c32  pea (0x24,PC) -> 0x00E16C58             ;   value = &0xFFFF
 *   00e16c38  move.l (-0xe,A6),D0 / lsr.l #5          ; bits 0..31 of delay, /32
 *   00e16c3e  move.w D0w,(-0x16,A6) / pea (-0x16,A6)  ;   value = &tick
 *   00e16c46  pea (0x12,PC) -> 0x00E16C5A             ; index = &3
 *   00e16c4a  jsr TIME_$WRT_TIMER
 *
 * Original address: 0x00e16bda, 126 bytes
 */

#include "time/time_internal.h"

/*
 * Constant cells at 0x00E16C58 (`gsk read`: ff ff 00 03), between this
 * procedure's rts (0x00E16C56) and TIME_$Q_INIT (0x00E16C5C).
 */
static const uint16_t time_$c_timer_max = 0xFFFF;   /* 0x00E16C58 */
static const uint16_t time_$c_timer_aux = 0x0003;   /* 0x00E16C5A */

void time_$q_setup_timer(time_queue_t *queue, clock_t *now)
{
    time_queue_elem_t *head;        /* A0 */
    clock_t delay;                  /* A6-0x10 */
    status_$t status;               /* A6-0x08, never read */
    uint16_t tick;                  /* A6-0x16 */
    uint32_t low32;

    /* 0x00E16BE4 */
    head = (time_queue_elem_t *)ARCH_VA_TO_PTR(queue->head);
    delay.high = head->expire_high;
    delay.low = head->expire_low;

    /* 0x00E16BFA: SUB48 is true (negative) when delay - *now >= 0 */
    if (SUB48(&delay, now) >= 0) {
        delay.high = 0;
        delay.low = 0;
    }

    /* 0x00E16C0E: Domain boolean, true = virtual-timer queue */
    if (queue->flags < 0) {
        /*
         * 0x00E16C14: a two-byte Pascal result slot is reserved and never
         * read (the frame is dropped by unlk).
         */
        PROC1_$SET_VT(queue->queue_id, &delay, &status);
    } else if ((delay.high >> 5) != 0) {
        /* 0x00E16C2A: more than 21 bits of ticks - saturate */
        TIME_$WRT_TIMER((uint16_t *)&time_$c_timer_aux,
                        (uint16_t *)&time_$c_timer_max);
    } else {
        /*
         * 0x00E16C38: the longword at (-0xe,A6) straddles the record - the
         * low 16 bits of `high` and all of `low` - i.e. bits 0..31 of the
         * 48-bit delay.  Divided by 32 and truncated to a word.
         */
        low32 = ((uint32_t)(delay.high & 0xFFFFu) << 16) | delay.low;
        tick = (uint16_t)(low32 >> 5);
        TIME_$WRT_TIMER((uint16_t *)&time_$c_timer_aux, &tick);
    }
}
