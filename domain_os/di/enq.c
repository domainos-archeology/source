/*
 * DI_$ENQ - Enqueue a deferred interrupt element
 *
 * Adds an element to the head of the deferred interrupt queue.
 * The element must not already be enqueued (crashes if it is).
 *
 * From: 0x00e209a8
 *
 * Original assembly:
 *   00e209a8    movea.l (0xc,SP),A0        ; Load elem pointer (3rd param)
 *   00e209ac    tst.b (0xc,A0)             ; Test enqueued flag
 *   00e209b0    beq.b 0x00e209ba           ; Branch if not enqueued
 *   00e209b2    pea (0x440,PC)             ; Push error status address
 *   00e209b6    bra.w 0x00e20b5a           ; Jump to CRASH_SYSTEM
 *   00e209ba    move.l (-0x3ba,PC),(A0)    ; elem->next = DI_$Q_HEAD
 *   00e209be    move.l A0,(0x00e20602).l   ; DI_$Q_HEAD = elem
 *   00e209c4    move.l (0x4,SP),(0x4,A0)   ; elem->arg1 = arg1
 *   00e209ca    move.l (0x8,SP),(0x8,A0)   ; elem->arg2 = arg2
 *   00e209d0    st (0xc,A0)                ; elem->enqueued = 0xFF
 *   00e209d4    rts
 *
 * DI_$Q_HEAD is at address 0x00e20602.
 */

#include "di/di.h"
#include "misc/crash_system.h"

/* Global queue head - points to first element in queue */
di_queue_elem_t *DI_$Q_HEAD = NULL;

/* Error status for double-enqueue condition */
static const status_$t Proc1_Bad_deferred_interrupt_queue_Err = 0x000D0001;

void DI_$ENQ(uint32_t arg1, uint32_t arg2, di_queue_elem_t *elem)
{
    /*
     * Check if already enqueued - this is a fatal error.
     * The original code crashes if the enqueued flag is non-zero.
     */
    if (elem->enqueued != 0) {
        CRASH_SYSTEM(&Proc1_Bad_deferred_interrupt_queue_Err);
        /* CRASH_SYSTEM does not return, but add unreachable loop
         * to match original decompiled code pattern */
        for (;;) {
            CRASH_SYSTEM(&Proc1_Bad_deferred_interrupt_queue_Err);
        }
    }

    /*
     * Insert at head of queue:
     * 1. New element's next points to current head
     * 2. Update head to point to new element
     * 3. Store callback arguments
     * 4. Mark as enqueued
     */
    elem->next = DI_$Q_HEAD;
    DI_$Q_HEAD = elem;
    elem->arg1 = arg1;
    elem->arg2 = arg2;
    elem->enqueued = 0xFF;
}
