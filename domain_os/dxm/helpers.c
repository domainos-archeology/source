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
 *   00e171fc  clr.l   -(SP)      ; vals[2] = 0
 *   00e171fe  clr.l   -(SP)      ; vals[1] = 0
 *   00e17200  move.l  D2,-(SP)   ; vals[0] = wait_val
 *   00e17202  pea     (A2)       ; ecs[2] = NULL
 *   00e17204  pea     (A2)       ; ecs[1] = NULL
 *   00e17206  pea     (A4)       ; ecs[0] = &queue->ec
 *   00e17208  jsr     EC_$WAIT   ; 0x00E20610
 *   00e1720e  lea     (0x18,SP),SP   ; caller pops all 24 bytes
 *   00e17212  pea     (A3)       ; queue
 *   00e17214  bsr.w   DXM_$SCAN_QUEUE
 *   00e17218  addq.w  #4,SP
 *   00e1721a  addq.l  #1,D2      ; wait_val++
 *   00e1721c  bra.b   loop
 *
 * EC_$WAIT takes two three-element arrays BY VALUE (24 bytes in all; see
 * 0x00E20610, which finds ecs at (0xC,SP) and vals at (0x18,SP)).  The
 * unused slots are NULL / 0, which is what terminates the scan at
 * 0x00E2061A.  The return value is discarded here.
 */
void DXM_$HELPER_COMMON(dxm_queue_t *queue)
{
    int32_t wait_val = 1;

    /* Infinite loop processing callbacks */
    for (;;) {
        /* Wait for event count to reach wait_val */
        EC_$WAIT((ec_$wait_ecs_t){ { &queue->ec, NULL, NULL } },
                 (ec_$wait_vals_t){ { wait_val, 0, 0 } });

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
