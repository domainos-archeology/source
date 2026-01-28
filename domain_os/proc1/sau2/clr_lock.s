/*
 * PROC1_$CLR_LOCK - Release a resource lock
 *
 * Releases a lock by clearing a bit in the process's resource_locks_held
 * bitmask. After releasing:
 * - Decrements lock depth counter
 * - If depth reaches zero, clears bit 0 of locks LSB (offset 0x43)
 * - Reorders ready list
 * - If all locks released, handles deferred operations:
 *   - Bit 4 of pri_max (0x55): deferred ready list removal
 *   - Bit 2 of pri_max (0x55): deferred suspend
 * - Dispatches to potentially higher priority process
 *
 * Crashes if the lock is not held by this process.
 *
 * Parameters:
 *   lock_id - Lock ID (0-31), passed on stack at 4(%sp)
 *
 * PCB offsets used:
 *   0x40: resource_locks_held (uint32_t bitmask)
 *   0x43: low byte of resource_locks_held (bit 0 = flag)
 *   0x55: pri_max (flags byte)
 *   0x5A: lock depth counter (uint16_t)
 *
 * Original address: 0x00e20b92
 */

        .text
        .even

/*
 * External references
 */
        .extern PROC1_$CURRENT_PCB
        .extern proc1_$reorder_if_needed
        .extern proc1_$remove_from_ready_list
        .extern FUN_00e20824
        .extern PROC1_$TRY_TO_SUSPEND
        .extern PROC1_$DISPATCH_INT2
        .extern CRASH_SYSTEM
        .extern Illegal_lock_err

/*
 * PROC1_$CLR_LOCK - Public entry point
 *
 * Gets lock_id from stack, loads current PCB, disables interrupts,
 * then falls through to body.
 */
        .global PROC1_$CLR_LOCK
PROC1_$CLR_LOCK:
        move.w  (0x4,%sp), %d0          /* lock_id from stack */
        movea.l PROC1_$CURRENT_PCB, %a1
        ori     #0x700, %sr             /* disable interrupts */

/*
 * proc1_$clr_lock_body - Internal entry
 *
 * Called with:
 *   D0.w = lock_id
 *   A1   = current PCB pointer
 *   Interrupts disabled
 */
        .global proc1_$clr_lock_body
proc1_$clr_lock_body:
        move.l  (0x40,%a1), %d1         /* D1 = resource_locks_held */
        bclr.l  %d0, %d1               /* clear lock bit */
        beq.s   .Lcrash                 /* crash if bit wasn't set */
        move.l  %d1, (0x40,%a1)         /* store updated locks */

        /* Decrement lock depth counter */
        subq.w  #1, (0x5a,%a1)
        beq.s   .Lclr_flag              /* if depth == 0, clear flag */
        bra.s   .Ldo_reorder

.Lclr_flag:
        /* Lock depth reached zero - clear bit 0 of resource_locks_held LSB */
        bclr.b  #0, (0x43,%a1)

.Ldo_reorder:
        bsr.w   proc1_$reorder_if_needed

        /* Check if all locks are released */
        tst.l   (0x40,%a1)
        bne.s   .Ldispatch              /* still have locks, just dispatch */

        /* All locks released - check for deferred operations */

        /* Check bit 4 of pri_max (deferred ready list removal) */
        bclr.b  #4, (0x55,%a1)
        beq.s   .Lcheck_suspend         /* bit wasn't set, skip */

        /* Deferred removal: remove from ready list and call FUN_00e20824 */
        bsr.w   proc1_$remove_from_ready_list
        bsr.w   FUN_00e20824

.Lcheck_suspend:
        /* Check bit 2 of pri_max (deferred suspend) */
        btst.b  #2, (0x55,%a1)
        beq.s   .Ldispatch              /* not set, just dispatch */

        /* Deferred suspend */
        pea     (%a1)                   /* push PCB pointer */
        jsr     PROC1_$TRY_TO_SUSPEND
        addq.w  #4, %sp                /* clean up stack */
        movea.l PROC1_$CURRENT_PCB, %a1 /* reload PCB (may have changed) */

.Ldispatch:
        bsr.w   PROC1_$DISPATCH_INT2
        andi    #0xF8FF, %sr            /* re-enable interrupts */
        rts

.Lcrash:
        pea     Illegal_lock_err
        jsr     CRASH_SYSTEM
        bra.s   .Lcrash                 /* loop forever (shouldn't return) */
