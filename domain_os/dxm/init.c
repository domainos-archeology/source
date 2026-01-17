/*
 * dxm/init.c - DXM_$INIT implementation
 *
 * Initializes the Deferred Execution Manager subsystem.
 *
 * Original address: 0x00E2FF50
 */

#include "dxm/dxm_internal.h"

/*
 * DXM_$INIT - Initialize DXM subsystem
 *
 * Initializes the event counts for both the wired and unwired queues.
 * The event counts are used to signal helper processes when new
 * callbacks have been added to their respective queues.
 *
 * Note: The queue structures (head, tail, mask, entries pointer)
 * are assumed to be pre-initialized elsewhere during system setup.
 * This function only initializes the event counts.
 *
 * Assembly (0x00E2FF50):
 *   link.w  A6,#0
 *   movea.l #0xe2a7c0,A0         ; A0 = base address
 *   pea     (0x62c,A0)           ; Push &wired_q.ec
 *   jsr     EC_$INIT
 *   addq.w  #4,SP
 *   movea.l #0xe2a7c0,A0         ; A0 = base address
 *   pea     (0x610,A0)           ; Push &unwired_q.ec
 *   jsr     EC_$INIT
 *   unlk    A6
 *   rts
 */
void DXM_$INIT(void)
{
    /* Initialize event count for wired queue */
    EC_$INIT(&DXM_$WIRED_Q.ec);

    /* Initialize event count for unwired queue */
    EC_$INIT(&DXM_$UNWIRED_Q.ec);
}
