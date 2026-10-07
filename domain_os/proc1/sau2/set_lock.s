/*
 * PROC1_$SET_LOCK - Acquire a resource lock
 *
 * Byte gate (source-6psc; tools/asm_compare.py, `make check'): encodings
 * identical to the image (modulo the documented widenings); address
 * operands resolve to our objects.
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
 *
 * TODO(source-2gk1): the image branches into shared ML code here (CLR_LOCK:
 * `beq.w 0x00E20EB0 / bra.w 0x00E20EB6' at 0x00E20BAE; SET_LOCK:
 * `bls.b 0x00E20B56' at 0x00E20AF8).  This file reproduces that shared
 * run inline, so it is instruction-equivalent but not byte-identical to
 * the image past that branch.
 * Original address: 0x00e20ae4
 */

        .section ".text.PROC1_$SET_LOCK","ax",@progbits
        .even

/*
 * External references
 */
        .extern PROC1_$CURRENT_PCB
        .extern proc1_$pcb_pool
        .set    PROC1_$READY_PCB, proc1_$pcb_pool + 0x68  /* 0xE1EC3A: PCB 2 nextp (proc1/proc1.h) */
        .extern proc1_$reorder_if_needed_int
        .extern CRASH_SYSTEM

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
        bls.s   set_lock_crash          /* ordering violation if mask <= held */

        /* Valid acquisition - set the lock bit */
        or.l    %d1, (0x40,%a1)

        /* If this process is the running process, skip reorder */
        cmpa.l  PROC1_$READY_PCB, %a1
        beq.s   .Ldone

        /* Not the running process - may need to reorder ready list */
        move    %sr, -(%sp)             /* save SR */
        ori     #0x700, %sr             /* disable interrupts */
        bsr.w   proc1_$reorder_if_needed_int
        move    (%sp)+, %sr             /* restore SR */

.Ldone:
        rts

/* The image's copy of this stub is at 0x00E20B56 (shared with
 * PROC1_$CLR_LOCK / ML_$UNLOCK); its loop re-enters the jsr, not the pea. */
set_lock_crash:
        pea     Illegal_lock_err
1:      jsr     CRASH_SYSTEM
        bra.s   1b                      /* 0x00E20B60 60f8 */

/*
 * Illegal_lock_err - the crash status longword at 0x00E20DE4 (0x000A0002),
 * reached by `pea (0x28c,PC)` at 0x00E20B56.  In the image it is a constant
 * cell in this module's own code region; ML_$UNLOCK's "lock not held" test
 * (`beq.b 0x00E20B56` at 0x00E20BA4) and PROC1_$CLR_LOCK share it.
 */
        .global Illegal_lock_err
        .even
Illegal_lock_err:
        .long   0x000A0002
