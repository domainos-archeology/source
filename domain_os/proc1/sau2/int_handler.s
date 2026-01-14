/*
 * PROC1_$INT_ADVANCE, PROC1_$INT_EXIT - Interrupt handler exit routines
 *
 * These functions handle the cleanup and potential rescheduling when
 * exiting from an interrupt handler.
 *
 * Original addresses:
 *   PROC1_$INT_ADVANCE: 0x00e208f6
 *   PROC1_$INT_EXIT:    0x00e208fe
 */

        .text
        .even

/*
 * External references
 */
        .extern PROC1_$CURRENT_PCB
        .extern PROC1_$READY_PCB
        .extern PROC1_$ATOMIC_OP_DEPTH
        .extern PROC1_$DISPATCH_INT3
        .extern ADVANCE_INT
        .extern FIM_$EXIT
        .extern DI_$Q_HEAD
        .extern IO_$SAVED_OS_SP
        .extern DAT_00e20606           /* DI in-progress flag */

/*
 * OS stack boundary
 */
        .set    OS_STACK_LIMIT, 0x00EB2BE8

/*
 * PROC1_$INT_ADVANCE - Called after advancing an event count from interrupt
 *
 * Disables interrupts, calls ADVANCE_INT, then checks if we need to
 * reschedule before returning to interrupted code.
 *
 * Entry: A0 = event count to advance
 */
        .globl  PROC1_$INT_ADVANCE
        .globl  _PROC1_$INT_ADVANCE
PROC1_$INT_ADVANCE:
_PROC1_$INT_ADVANCE:
        ori.w   #0x0700, %sr            /* Disable interrupts */
        bsr.w   ADVANCE_INT             /* Advance the event count */
        /* Fall through to common exit path */

/*
 * Common interrupt exit processing
 * Checks interrupt level on stack to determine if we can reschedule
 */
int_exit_check:
        moveq   #0x07, %d0
        and.b   0x10(%sp), %d0          /* Get interrupt level from SR on stack */
        beq.s   int_exit_level0         /* If level 0, can reschedule */

        /* Interrupted at elevated level - check for OS stack */
        cmpa.l  #OS_STACK_LIMIT, %sp
        bne.w   int_exit_return
        /* On OS stack - restore saved SP */
        movea.l IO_$SAVED_OS_SP, %sp
        clr.l   IO_$SAVED_OS_SP
        bra.s   int_exit_return

int_exit_level0:
        /* Check if deferred interrupt work in progress */
        tst.b   DAT_00e20606
        bne.s   int_exit_return         /* If in progress, just return */

        /* Process deferred interrupt queue */
di_loop:
        move.l  DI_$Q_HEAD, %d0
        beq.s   di_done                 /* Queue empty, done */

        /* Process queue entry */
        movea.l %d0, %a1
        move.l  (%a1), DI_$Q_HEAD       /* Unlink from queue */
        movea.l 4(%a1), %a0             /* Callback function */
        move.l  8(%a1), -(%sp)          /* Push argument */
        sf      12(%a1)                 /* Clear entry active flag */
        st      DAT_00e20606            /* Set in-progress flag */
        andi.w  #0xF8FF, %sr            /* Enable interrupts */
        jsr     (%a0)                   /* Call callback */
        addq.l  #4, %sp
        ori.w   #0x0700, %sr            /* Disable interrupts */
        sf      DAT_00e20606            /* Clear in-progress flag */
        move.l  %a0, %d0                /* Check if callback returned EC to advance */
        beq.s   di_loop
        bsr.w   ADVANCE_INT             /* Advance returned EC */
        bra.s   di_loop

di_done:
        /* Check for OS stack */
        cmpa.l  #OS_STACK_LIMIT, %sp
        bne.s   check_reschedule
        movea.l IO_$SAVED_OS_SP, %sp
        clr.l   IO_$SAVED_OS_SP

check_reschedule:
        /* Check if reschedule needed */
        movea.l PROC1_$CURRENT_PCB, %a1
        movea.l PROC1_$READY_PCB, %a0
        cmpa.l  %a0, %a1
        beq.s   int_exit_return         /* Same process, no switch */

        /* Check atomic operation depth */
        move.w  PROC1_$ATOMIC_OP_DEPTH, %d0
        bne.s   atomic_deferred         /* Can't switch now */

        /* Perform context switch */
        bsr.w   PROC1_$DISPATCH_INT3
        bra.s   int_exit_return

atomic_deferred:
        /* Mark that switch was deferred */
        blt.s   int_exit_return         /* If negative, already marked */
        sub.w   #-0x7FFF, %d0           /* Add 0x7FFF to mark deferred */
        move.w  %d0, PROC1_$ATOMIC_OP_DEPTH
        /* Fall through to return */

int_exit_return:
        movem.l (%sp)+, %d0-%d1/%a0-%a1 /* Restore scratch registers */
        jmp     FIM_$EXIT               /* Return from interrupt */

/*
 * PROC1_$INT_EXIT - Standard interrupt exit routine
 *
 * Called at the end of interrupt handlers to check for rescheduling
 * and process deferred work.
 *
 * Entry: SR on stack at offset 0x10 (standard interrupt frame)
 */
        .globl  PROC1_$INT_EXIT
        .globl  _PROC1_$INT_EXIT
PROC1_$INT_EXIT:
_PROC1_$INT_EXIT:
        ori.w   #0x0700, %sr            /* Disable interrupts */
        bra.s   int_exit_check          /* Join common exit path */
