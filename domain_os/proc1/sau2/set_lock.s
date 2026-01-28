/*
 * PROC1_$SET_LOCK - Acquire a resource lock
 *
 * Acquires a lock by setting a bit in the process's resource_locks_held
 * bitmask. The lock is identified by an ID (0-31).
 *
 * If the new lock has higher priority (higher bit position) than any
 * currently held lock, the process may be reordered in the ready list.
 *
 * Crashes if lock ordering is violated (new lock bit <= held locks).
 *
 * Parameters:
 *   lock_id - Lock ID (0-31), passed on stack at 4(%sp)
 *
 * Entry points:
 *   PROC1_$SET_LOCK      - Public API entry (0x00e20ae4)
 *   proc1_$set_lock_body - Internal entry with lock_id in D0 (0x00e20ae8)
 *
 * PCB offsets used:
 *   0x40: resource_locks_held (uint32_t bitmask)
 *   0x5A: lock depth counter (uint16_t)
 *
 * Original address: 0x00e20ae4
 */

        .text
        .even

/*
 * External references
 */
        .extern PROC1_$CURRENT_PCB
        .extern PROC1_$READY_PCB
        .extern proc1_$reorder_if_needed
        .extern CRASH_SYSTEM
        .extern Illegal_lock_err

/*
 * PROC1_$SET_LOCK - Public entry point
 *
 * Gets lock_id from stack and falls through to body.
 */
        .global PROC1_$SET_LOCK
PROC1_$SET_LOCK:
        move.w  (0x4,%sp), %d0          /* lock_id from stack */
        /* fall through to body */

/*
 * proc1_$set_lock_body - Internal entry
 *
 * Called with lock_id in D0.w
 * Also used as internal entry from other assembly routines.
 */
        .global proc1_$set_lock_body
proc1_$set_lock_body:
        movea.l PROC1_$CURRENT_PCB, %a1

        /* Increment lock depth counter at PCB+0x5A */
        addq.w  #1, (0x5a,%a1)

        /* Build lock mask: D1 = 1 << lock_id */
        moveq   #0, %d1
        bset.l  %d0, %d1

        /*
         * Check lock ordering: the new lock bit must be strictly
         * greater than all currently held locks. If mask <= held,
         * this is a lock ordering violation.
         */
        cmp.l   (0x40,%a1), %d1         /* compare mask vs resource_locks_held */
        bls.s   .Lcrash                 /* ordering violation if mask <= held */

        /* Valid acquisition - set the lock bit */
        or.l    %d1, (0x40,%a1)

        /* If this process is the running process, skip reorder */
        cmpa.l  PROC1_$READY_PCB, %a1
        beq.s   .Ldone

        /* Not the running process - may need to reorder ready list */
        move    %sr, -(%sp)             /* save SR */
        ori     #0x700, %sr             /* disable interrupts */
        bsr.w   proc1_$reorder_if_needed
        move    (%sp)+, %sr             /* restore SR */

.Ldone:
        rts

.Lcrash:
        pea     Illegal_lock_err
        jsr     CRASH_SYSTEM
        bra.s   .Lcrash                 /* loop forever (shouldn't return) */
