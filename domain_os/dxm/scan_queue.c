/*
 * dxm/scan_queue.c - DXM_$SCAN_QUEUE implementation
 *
 * Processes all pending callbacks in a DXM queue.
 *
 * Original address: 0x00E17168
 */

#include "dxm/dxm_internal.h"

/*
 * DXM_$SCAN_QUEUE - Process all pending callbacks in a queue
 *
 * Dequeues and executes all pending callbacks in the specified queue.
 * This is called by the helper processes after being signaled that
 * new callbacks have been added.
 *
 * The function loops while there are entries in the queue (head != tail):
 * 1. Lock the queue
 * 2. If queue is empty, unlock and return
 * 3. Get the callback and data from the head entry
 * 4. Advance head pointer
 * 5. Unlock the queue
 * 6. Call the callback with a pointer to the data
 *
 * Parameters:
 *   queue - Queue to scan
 *
 * Assembly (0x00E17168):
 *   link.w  A6,#-0x14
 *   movem.l {A2 D2},-(SP)
 *   movea.l (0x8,A6),A2          ; A2 = queue
 *   bra.b   check_loop
 * process_entry:
 *   move.w  (A2),D0w             ; D0 = head
 *   movea.l (0x18,A2),A0         ; A0 = entries base
 *   lsl.w   #4,D0w               ; D0 = head * 16
 *   lea     (0,A0,D0w),A0        ; A0 = &entries[head]
 *   move.l  (A0),D2              ; D2 = callback
 *   lea     (0x4,A0),A1          ; A1 = &entry->data
 *   move.l  A1,(-0xc,A6)         ; local_10 = &data
 *   move.w  (A2),D0w             ; D0 = head
 *   addq.w  #1,D0w               ; D0 = head + 1
 *   and.w   (0x4,A2),D0w         ; D0 = (head + 1) & mask
 *   move.w  D0w,(A2)             ; head = D0
 *   ; Unlock
 *   subq.l  #2,SP
 *   move.w  (-0xe,A6),-(SP)      ; push token
 *   pea     (0x8,A2)             ; push &lock
 *   jsr     ML_$SPIN_UNLOCK
 *   addq.w  #8,SP
 *   ; Call callback
 *   pea     (-0xc,A6)            ; push &local_10 (pointer to data pointer)
 *   movea.l D2,A1
 *   jsr     (A1)
 *   addq.w  #4,SP
 * check_loop:
 *   pea     (0x8,A2)             ; push &lock
 *   jsr     ML_$SPIN_LOCK
 *   addq.w  #4,SP
 *   move.w  D0w,(-0xe,A6)        ; token = D0
 *   move.w  (A2),D1w             ; D1 = head
 *   cmp.w   (0x2,A2),D1w         ; Compare with tail
 *   bne.b   process_entry        ; If head != tail, process entry
 *   ; Queue empty, unlock and return
 *   subq.l  #2,SP
 *   move.w  D0w,-(SP)
 *   pea     (0x8,A2)
 *   jsr     ML_$SPIN_UNLOCK
 *   movem.l (-0x1c,A6),{D2 A2}
 *   unlk    A6
 *   rts
 */
void DXM_$SCAN_QUEUE(dxm_queue_t *queue)
{
    uint16_t token;
    dxm_entry_t *entry;
    void (*callback)(void *);
    void *data_ptr;

    for (;;) {
        /* Lock the queue */
        token = ML_$SPIN_LOCK(&queue->lock);

        /* Check if queue is empty */
        if (queue->head == queue->tail) {
            /* Queue is empty, unlock and return */
            ML_$SPIN_UNLOCK(&queue->lock, token);
            return;
        }

        /* Get the entry at head position */
        entry = (dxm_entry_t *)((char *)queue->entries +
                                ((int16_t)queue->head << 4));

        /* Extract callback and save data pointer */
        callback = entry->callback;
        data_ptr = entry->data;

        /* Advance head pointer with wraparound */
        queue->head = (queue->head + 1) & queue->mask;

        /* Unlock the queue before calling callback */
        ML_$SPIN_UNLOCK(&queue->lock, token);

        /* Call the callback with pointer to data
         * Note: Original passes pointer to local containing data_ptr,
         * giving callback access to the 12 bytes of data */
        callback(&data_ptr);
    }
}
