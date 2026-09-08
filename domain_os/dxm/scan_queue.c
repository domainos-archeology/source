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
    dxm_$callback_fn_t callback;
    uint8_t *data_ptr;      /* A6-0x0C: the address of entry->data */

    for (;;) {
        /* Lock the queue */
        token = ML_$SPIN_LOCK(&queue->lock);

        /* Check if queue is empty */
        if (queue->head == queue->tail) {
            /* Queue is empty, unlock and return */
            ML_$SPIN_UNLOCK(&queue->lock, token);
            return;
        }

        /*
         * 0x00E17178-0x00E17180: the entry at the head position.  The index
         * is scaled with a WORD shift ("lsl.w #0x4,D0w") and then used as a
         * sign-extended word index ("lea (0x0,A0,D0w*0x1),A0"), which is
         * spelled out here rather than relying on int-width arithmetic.
         */
        entry = (dxm_entry_t *)((uint8_t *)queue->entries +
                                (int32_t)(int16_t)(uint16_t)
                                    ((uint16_t)queue->head << 4));

        /*
         * 0x00E17184: the entry holds a 4-byte code address (source-wy9y);
         * dxm_$callback_fn() turns it back into something callable on the
         * host.
         */
        callback = dxm_$callback_fn(entry->callback);

        /*
         * 0x00E17186-0x00E1718A: "lea (0x4,A0),A1 / move.l A1,(-0xc,A6)".
         * The frame local holds the ADDRESS of the queue slot's data, not a
         * copy of it, and 0x00E171AA hands the callback the address OF THAT
         * LOCAL ("pea (-0xc,A6)").  So the callback is given a pointer to a
         * pointer, and through it it can read and write the twelve data
         * bytes still sitting in the queue entry - the slot has already been
         * released by the head advance below, but its bytes are untouched
         * until DXM_$ADD_CALLBACK reuses it.
         */
        data_ptr = entry->data;

        /* 0x00E1718E-0x00E17196: advance head with wraparound */
        queue->head = (queue->head + 1) & queue->mask;

        /* 0x00E17198-0x00E171A8: unlock before calling the callback */
        ML_$SPIN_UNLOCK(&queue->lock, token);

        /* 0x00E171AA-0x00E171B0 */
        callback(&data_ptr);
    }
}
