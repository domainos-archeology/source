/*
 * TIME_$VT_INT - Virtual timer interrupt handler
 *
 * Called when the virtual timer fires. Updates process virtual time
 * and scans the VT queue for the current process.
 *
 * Original address: 0x00e163e4
 *
 * Assembly:
 *   00e163e4    link.w A6,-0x10
 *   00e163e8    pea (A5)
 *   00e163ea    lea (0xe29198).l,A5       ; Base of queue area
 *   00e163f0    pea (-0x8,A6)
 *   00e163f4    jsr 0x00e1491e.l          ; PROC1_$VT_INT
 *   00e163fa    addq.w #0x4,SP
 *   00e163fc    pea (-0xc,A6)
 *   00e16400    pea (-0x8,A6)
 *   00e16404    move.w PROC1_$CURRENT,D0w
 *   00e1640a    lsl.w #0x2,D0w            ; *4
 *   00e1640c    move.w D0w,D1w
 *   00e1640e    add.w D1w,D1w             ; *8
 *   00e16410    add.w D1w,D0w             ; *12
 *   00e16412    lea (0x0,A5,D0w*0x1),A0   ; base + proc*12
 *   00e16416    pea (0x12fc,A0)           ; + VT_QUEUE_OFFSET
 *   00e1641a    jsr TIME_$Q_SCAN_QUEUE
 *   00e16420    clr.b IN_VT_INT
 */

#include "time.h"
#include "proc1/proc1.h"

/* External references */
extern void PROC1_$VT_INT(clock_t *vt_clock);
extern uint16_t PROC1_$CURRENT;

/* Base of VT queue array and offset */
#define VT_QUEUE_ARRAY_BASE 0xE29198
#define VT_QUEUE_OFFSET     0x12FC

void TIME_$VT_INT(void)
{
    clock_t vt_clock;
    uint32_t arg;
    time_queue_t *vt_queue;

    /* Update process virtual time */
    PROC1_$VT_INT(&vt_clock);

    /*
     * Calculate address of this process's VT queue:
     * base + (PROC1_$CURRENT * 12) + VT_QUEUE_OFFSET
     */
    vt_queue = (time_queue_t *)((char *)VT_QUEUE_ARRAY_BASE +
                                 (PROC1_$CURRENT * 12) +
                                 VT_QUEUE_OFFSET);

    /* Scan the VT queue and fire expired callbacks */
    TIME_$Q_SCAN_QUEUE(vt_queue, &vt_clock, &arg);

    /* Clear interrupt-in-progress flag */
    IN_VT_INT = 0;
}
