/*
 * time_$q_setup_timer - Setup hardware timer for next queue element
 *
 * Programs the hardware timer based on the expiration time of the head
 * element in the queue. Called after inserting an element at the head
 * of a time queue.
 *
 * For VT queues (queue->flags bit 7 set):
 *   - Sets up the virtual timer via PROC1_$SET_VT
 *
 * For RTE queues:
 *   - Calculates time until expiration
 *   - Divides by 32 to convert to timer ticks
 *   - Programs timer 3 (auxiliary timer) with the countdown value
 *   - If countdown too large, uses max value (0xFFFF)
 *
 * Parameters:
 *   queue - Pointer to time queue
 *   when  - Pointer to current time (clock_t)
 *
 * Original address: 0x00e16bda
 *
 * Assembly:
 *   00e16bda    link.w A6,-0x18
 *   00e16bde    pea (A2)
 *   00e16be0    movea.l (0x8,A6),A2          ; A2 = queue
 *   00e16be4    movea.l (A2),A0              ; A0 = queue->head (first element)
 *   00e16be6    move.l (0xc,A0),(-0x10,A6)   ; local_14.high = elem->expire_high
 *   00e16bec    move.w (0x10,A0),(-0xc,A6)   ; local_14.low = elem->expire_low
 *   00e16bf2    move.l (0xc,A6),-(SP)        ; Push &when
 *   00e16bf6    pea (-0x10,A6)               ; Push &local_14
 *   00e16bfa    jsr SUB48                    ; local_14 -= when
 *   00e16c00    addq.w #0x8,SP
 *   00e16c02    tst.b D0b                    ; Check sign of result
 *   00e16c04    bmi.b skip                   ; If negative (expire > now), skip clear
 *   00e16c06    clr.l (-0x10,A6)             ; If >= 0 (now >= expire), set delay to 0
 *   00e16c0a    clr.w (-0xc,A6)
 *   00e16c0e    tst.b (0x8,A2)               ; Check queue->flags
 *   00e16c12    bpl.b rte_path               ; If bit 7 clear, use RTE timer
 *   ; VT path...
 *   00e16c22    jsr PROC1_$SET_VT
 *   00e16c28    bra.b done
 *   ; RTE path...
 *   00e16c2a    move.l (-0x10,A6),D0         ; D0 = delay.high
 *   00e16c2e    lsr.l #0x5,D0                ; D0 >>= 5 (divide by 32)
 *   00e16c30    beq.b fits                   ; If high word fits, calculate tick value
 *   00e16c32    pea (0x24,PC)                ; Else push &0xFFFF (max value)
 *   00e16c36    bra.b write_timer
 *   00e16c38    move.l (-0xe,A6),D0          ; D0 = high_word_low_half | low_word
 *   00e16c3c    lsr.l #0x5,D0                ; Divide by 32
 *   00e16c3e    move.w D0w,(-0x16,A6)        ; Store as local tick value
 *   00e16c42    pea (-0x16,A6)               ; Push &local_tick
 *   00e16c46    pea (0x12,PC)                ; Push &timer_index_3 (0x0003)
 *   00e16c4a    jsr TIME_$WRT_TIMER
 */

#include "time/time_internal.h"

/* Timer index for auxiliary timer (used for RTE scheduling) */
static const uint16_t timer_index_aux = 3;

/* Maximum timer value when delay is too large */
static const uint16_t timer_max_value = 0xFFFF;

void time_$q_setup_timer(time_queue_t *queue, clock_t *when)
{
    time_queue_elem_t *head;
    clock_t delay;
    status_$t status;
    uint16_t tick_value;

    /* Get head element from queue
     * Note: head is stored as uint32_t (M68K pointer), cast via uintptr_t for portability
     */
    head = (time_queue_elem_t *)(uintptr_t)queue->head;

    /* Copy expiration time from head element */
    delay.high = head->expire_high;
    delay.low = head->expire_low;

    /*
     * Subtract current time from expiration time
     * SUB48 returns negative if result is negative (delay > 0)
     * Returns non-negative if result is >= 0 (already expired)
     */
    if (SUB48(&delay, when) >= 0) {
        /* Already expired - set delay to zero */
        delay.high = 0;
        delay.low = 0;
    }

    /* Check queue type via flags bit 7 */
    if ((int8_t)queue->flags < 0) {
        /*
         * VT queue (flags bit 7 set)
         * Set up virtual timer via PROC1_$SET_VT
         * queue_id at offset 0x0A contains the process ID
         *
         * Note: PROC1_$SET_VT extracts just the low 16 bits from the clock_t,
         * or uses -1 if high word is non-zero. Cast to match existing prototype.
         */
        PROC1_$SET_VT(queue->queue_id, (uint32_t *)&delay, &status);
    } else {
        /*
         * RTE queue (flags bit 7 clear)
         * Calculate timer tick value by dividing delay by 32
         *
         * The 48-bit delay is divided by 32 (right shift 5)
         * If the high word (after shift) is non-zero, delay is too large,
         * so use maximum timer value instead.
         */
        if ((delay.high >> 5) != 0) {
            /* Delay too large - use max value */
            TIME_$WRT_TIMER((uint16_t *)&timer_index_aux, (uint16_t *)&timer_max_value);
        } else {
            /*
             * Calculate tick value from middle 16 bits of delay
             * This is essentially: (delay.high << 11) | (delay.low >> 5)
             * Or equivalently: ((delay >> 5) & 0xFFFF)
             *
             * Note: The assembly reads from offset -0xe which spans
             * the low 16 bits of high and the full low word, then shifts.
             */
            uint32_t combined = ((delay.high & 0xFFFF) << 16) | delay.low;
            tick_value = (uint16_t)(combined >> 5);
            TIME_$WRT_TIMER((uint16_t *)&timer_index_aux, &tick_value);
        }
    }
}
