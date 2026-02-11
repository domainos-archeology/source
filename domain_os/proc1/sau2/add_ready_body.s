/*
 * proc1_$add_ready_body - Priority-ordered ready list insertion
 *
 * Body of PROC1_$ADD_READY that uses register calling convention.
 * Walks the ready list comparing resource_locks_held (offset 0x40)
 * and priority (offset 0x52), inserting the new PCB in FIFO order
 * within the same priority level.
 *
 * This contrasts with proc1_$insert_into_ready_list (at 0xe20844)
 * which uses LIFO ordering within same priority (bcs vs bls).
 *
 * Register inputs:
 *   A1 = Pointer to PCB to insert into ready list
 *
 * PROC1_$ADD_READY at 0xe20820 is a 4-byte wrapper:
 *   movea.l (0x4,%sp), %a1
 * that falls through into this function.
 *
 * Called directly from:
 *   - proc1_$clr_lock_body (clr_lock.s) after deferred ready list update
 *   - ML_$UNLOCK exit path
 *   - ML_$EXCLUSION_STOP exit path
 *   - PROC1_$INHIBIT_END
 *
 * Original address: 0x00e20824
 * Size: 32 bytes
 */

        .text
        .even

        .extern PROC1_$READY_PCB
        .extern PROC1_$READY_COUNT

        .global proc1_$add_ready_body
proc1_$add_ready_body:
        /* Load comparison values from input PCB */
        move.l  (0x40,%a1), %d1         /* D1 = A1->resource_locks_held */
        move.w  (0x52,%a1), %d0         /* D0.w = A1->state (priority portion) */

        /* Start scanning from head of ready list */
        movea.l PROC1_$READY_PCB, %a0

        bra.b   .Lcompare

.Lnext:
        movea.l (%a0), %a0              /* A0 = A0->nextp */

.Lcompare:
        /* Compare resource_locks_held */
        cmp.l   (0x40,%a0), %d1
        bhi.b   .Linsert                /* A1's locks > A0's: insert here */
        bne.b   .Lnext                  /* A1's locks < A0's: keep searching */

        /* Equal locks_held - compare priority (FIFO: insert after equal) */
        cmp.w   (0x52,%a0), %d0
        bls.b   .Lnext                  /* A1's pri <= A0's: keep searching */

.Linsert:
        /* Insert A1 before A0 in the doubly-linked list */
        move.l  %a0, (%a1)              /* A1->nextp = A0 */
        move.l  (0x4,%a0), (0x4,%a1)    /* A1->prevp = A0->prevp */
        move.l  %a1, (0x4,%a0)          /* A0->prevp = A1 */
        movea.l (0x4,%a1), %a0          /* A0 = A1->prevp (old A0->prevp) */
        move.l  %a1, (%a0)              /* A0(old prev)->nextp = A1 */

        /* Increment ready count */
        addq.w  #1, PROC1_$READY_COUNT

        rts
