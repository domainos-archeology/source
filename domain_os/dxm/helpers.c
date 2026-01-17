/*
 * dxm/helpers.c - DXM helper process entry points
 *
 * These functions are the main loops for DXM helper processes.
 * They wait for callbacks to be added and then execute them.
 *
 * Original addresses:
 *   DXM_$HELPER_COMMON:   0x00E171E4
 *   DXM_$HELPER_WIRED:    0x00E1721E
 *   DXM_$HELPER_UNWIRED:  0x00E17246
 */

#include "dxm/dxm_internal.h"

/*
 * DXM_$HELPER_COMMON - Common helper process loop
 *
 * Waits on the queue's event count and calls DXM_$SCAN_QUEUE
 * when callbacks are added. Runs in an infinite loop.
 *
 * This function never returns. It continuously:
 * 1. Waits for the queue's event count to reach wait_val
 * 2. Calls DXM_$SCAN_QUEUE to process all pending callbacks
 * 3. Increments wait_val and repeats
 *
 * Parameters:
 *   queue - Queue to service
 *
 * Assembly (0x00E171E4):
 *   link.w  A6,#-0x4
 *   movem.l {A4 A3 A2 D2},-(SP)
 *   movea.l (0x8,A6),A3          ; A3 = queue
 *   moveq   #1,D2                ; D2 = wait_val = 1
 *   movea.l #0,A2                ; A2 = NULL (unused)
 *   lea     (0xc,A3),A4          ; A4 = &queue->ec
 * loop:
 *   clr.l   -(SP)                ; Push 0 (ec5)
 *   clr.l   -(SP)                ; Push 0 (ec4)
 *   move.l  D2,-(SP)             ; Push wait_val
 *   pea     (A2)                 ; Push NULL (ec2)
 *   pea     (A2)                 ; Push NULL (ec1)
 *   pea     (A4)                 ; Push &queue->ec
 *   jsr     EC_$WAIT
 *   lea     (0x18,SP),SP
 *   pea     (A3)                 ; Push queue
 *   bsr.w   DXM_$SCAN_QUEUE
 *   addq.w  #4,SP
 *   addq.l  #1,D2                ; wait_val++
 *   bra.b   loop
 */
void DXM_$HELPER_COMMON(dxm_queue_t *queue)
{
    int32_t wait_val = 1;
    ec_$eventcount_t *ecs[6];

    /* Set up event count array - only first entry is used */
    ecs[0] = &queue->ec;
    ecs[1] = NULL;
    ecs[2] = NULL;
    ecs[3] = NULL;
    ecs[4] = NULL;
    ecs[5] = NULL;

    /* Infinite loop processing callbacks */
    for (;;) {
        /* Wait for event count to reach wait_val */
        EC_$WAIT(ecs, &wait_val);

        /* Process all pending callbacks */
        DXM_$SCAN_QUEUE(queue);

        /* Increment wait value for next iteration */
        wait_val++;
    }
}

/*
 * DXM_$HELPER_WIRED - Wired helper process entry point
 *
 * Sets resource lock 0x0D and services the wired queue.
 * This function never returns.
 *
 * The wired helper holds the wired lock (0x0D) which prevents
 * preemption by lower-priority processes, making it suitable
 * for time-critical callbacks.
 *
 * Assembly (0x00E1721E):
 *   link.w  A6,#0
 *   pea     (A5)
 *   lea     (0xe2a7c0).l,A5      ; Set up A5 for data access
 *   subq.l  #2,SP
 *   move.w  #0xd,-(SP)           ; Push lock ID 0x0D
 *   jsr     PROC1_$SET_LOCK
 *   addq.w  #4,SP
 *   pea     (0x620,A5)           ; Push &DXM_$WIRED_Q
 *   bsr.b   DXM_$HELPER_COMMON
 *   ; Never returns
 */
void DXM_$HELPER_WIRED(void)
{
    /* Acquire the wired lock */
    PROC1_$SET_LOCK(DXM_WIRED_LOCK_ID);

    /* Service the wired queue forever */
    DXM_$HELPER_COMMON(&DXM_$WIRED_Q);
}

/*
 * DXM_$HELPER_UNWIRED - Unwired helper process entry point
 *
 * Sets resource lock 0x03 and services the unwired queue.
 * This function never returns.
 *
 * The unwired helper holds lock 0x03 which provides basic
 * scheduling priority without the strict requirements of
 * the wired helper.
 *
 * Assembly (0x00E17246):
 *   link.w  A6,#0
 *   pea     (A5)
 *   lea     (0xe2a7c0).l,A5      ; Set up A5 for data access
 *   subq.l  #2,SP
 *   move.w  #0x3,-(SP)           ; Push lock ID 0x03
 *   jsr     PROC1_$SET_LOCK
 *   addq.w  #4,SP
 *   pea     (0x604,A5)           ; Push &DXM_$UNWIRED_Q
 *   bsr.w   DXM_$HELPER_COMMON
 *   ; Never returns
 */
void DXM_$HELPER_UNWIRED(void)
{
    /* Acquire the unwired lock */
    PROC1_$SET_LOCK(DXM_UNWIRED_LOCK_ID);

    /* Service the unwired queue forever */
    DXM_$HELPER_COMMON(&DXM_$UNWIRED_Q);
}
