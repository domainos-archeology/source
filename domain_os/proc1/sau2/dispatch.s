/*
 * PROC1_$DISPATCH_* - Process dispatcher functions
 *
 * These functions implement context switching between processes
 * on the m68k architecture.
 *
 * Entry points:
 *   PROC1_$DISPATCH      - High-level dispatch with interrupt clear
 *   PROC1_$DISPATCH_INT  - Dispatch with current PCB in A1
 *   PROC1_$DISPATCH_INT2 - Dispatch with A1=current PCB, entry for specific PCB
 *   PROC1_$DISPATCH_INT3 - Main context switch (A1=current, A0=ready)
 *
 * PCB layout used:
 *   0x08-0x34: Saved registers D2-D7, A2-A6, A7(SP)
 *   0x38: Saved USP
 *   0x44: mypid
 *   0x46: asid
 *   0x48: vtimer
 *   0x4C: cpu_total
 *   0x50: cpu_usage
 *
 * Original addresses:
 *   PROC1_$DISPATCH:      0x00e20a18
 *   PROC1_$DISPATCH_INT:  0x00e20a20
 *   PROC1_$DISPATCH_INT2: 0x00e20a24
 *   PROC1_$DISPATCH_INT3: 0x00e20a34
 */

        .text
        .even

/*
 * External references
 */
        .extern PROC1_$CURRENT_PCB
        .extern PROC1_$READY_PCB
        .extern PROC1_$CURRENT
        .extern PROC1_$ATOMIC_OP_DEPTH
        .extern TIME_$VT_TIMER
        .extern TIME_$WRT_VT_TIMER
        .extern MMU_$INSTALL_ASID
        .extern MMAP_$SET_WS_PRI
        .extern CRASH_SYSTEM
        .extern Bad_atomic_operation_err

/*
 * Memory-mapped register
 */
        .set    MMU_STATUS_REG, 0x00FFB403

/*
 * Error handling - crash if dispatcher called during atomic operation
 */
atomic_op_error:
        pea     Bad_atomic_operation_err
        jsr     CRASH_SYSTEM
        bra.s   atomic_op_error         /* Loop forever (shouldn't return) */

/*
 * PROC1_$DISPATCH - High-level dispatch entry point
 *
 * Calls DISPATCH_INT then clears interrupt mask and returns.
 * This is the normal entry point when you want to potentially
 * switch to another process.
 */
        .globl  PROC1_$DISPATCH
        .globl  _PROC1_$DISPATCH
PROC1_$DISPATCH:
_PROC1_$DISPATCH:
        bsr.s   PROC1_$DISPATCH_INT
        andi.w  #0xF8FF, %sr            /* Clear interrupt mask */
        rts

/*
 * PROC1_$DISPATCH_INT - Dispatch with current PCB lookup
 *
 * Loads PROC1_$CURRENT_PCB into A1 and falls through to INT2.
 * Called when you don't already have the current PCB pointer.
 */
        .globl  PROC1_$DISPATCH_INT
        .globl  _PROC1_$DISPATCH_INT
PROC1_$DISPATCH_INT:
_PROC1_$DISPATCH_INT:
        movea.l PROC1_$CURRENT_PCB, %a1 /* Load current PCB into A1 */
        /* Fall through to DISPATCH_INT2 */

/*
 * PROC1_$DISPATCH_INT2 - Dispatch with A1 = current PCB
 *
 * Compares current PCB with ready PCB. If they're different,
 * performs context switch via INT3. Otherwise, just returns.
 *
 * Also checks atomic operation depth - crashes if non-zero.
 */
        .globl  PROC1_$DISPATCH_INT2
        .globl  _PROC1_$DISPATCH_INT2
PROC1_$DISPATCH_INT2:
_PROC1_$DISPATCH_INT2:
        movea.l PROC1_$READY_PCB, %a0   /* Load ready PCB into A0 */
        cmpa.l  %a0, %a1                /* Compare current with ready */
        beq.w   dispatch_done           /* If equal, nothing to do */
        move.w  PROC1_$ATOMIC_OP_DEPTH, %d0
        bne.s   atomic_op_error         /* Crash if in atomic op */
        /* Fall through to DISPATCH_INT3 */

/*
 * PROC1_$DISPATCH_INT3 - Main context switch
 *
 * Saves all callee-saved registers and USP from current process (A1),
 * updates CPU time accounting, then restores state of ready process (A0).
 *
 * Register usage:
 *   A0 = ready PCB (new process to run)
 *   A1 = current PCB (process being switched out)
 *   A3 = saved ready PCB pointer
 *   A4 = saved current PCB pointer (used across function calls)
 */
        .globl  PROC1_$DISPATCH_INT3
        .globl  _PROC1_$DISPATCH_INT3
PROC1_$DISPATCH_INT3:
_PROC1_$DISPATCH_INT3:
        /* Save callee-saved registers to current PCB */
        movem.l %d2-%d7/%a2-%a6/%sp, 8(%a1)  /* Save D2-D7, A2-A6, SP at offsets 0x08-0x34 */
        move.l  %usp, %a2
        move.l  %a2, 0x38(%a1)          /* Save USP */

        /* Preserve pointers across function calls */
        movea.l %a0, %a3                /* A3 = ready PCB */
        movea.l %a1, %a4                /* A4 = current PCB */

        /* Get current virtual timer value */
        jsr     TIME_$VT_TIMER          /* Returns timer value in D0.W */

        /* Update CPU time accounting for outgoing process */
        move.w  0x48(%a4), %d1          /* D1 = pcb->vtimer */
        sub.w   %d0, %d1                /* D1 = elapsed time */
        add.w   %d1, 0x50(%a4)          /* Add to cpu_usage */
        bcc.s   1f                      /* Branch if no carry */
        addq.l  #1, 0x4C(%a4)           /* Increment cpu_total on overflow */
1:
        move.w  %d0, 0x48(%a4)          /* Save new vtimer value */

        /* Switch to new process */
        movea.l %a3, %a4                /* A4 = ready PCB (new process) */
        move.w  0x48(%a4), %d0          /* D0 = new process vtimer */
        jsr     TIME_$WRT_VT_TIMER      /* Write virtual timer */

        /* Restore USP from new process */
        movea.l 0x38(%a4), %a2
        move.l  %a2, %usp

        /* Update global current process state */
        move.l  %a4, PROC1_$CURRENT_PCB /* Set current PCB pointer */
        move.w  0x44(%a4), PROC1_$CURRENT  /* Set current PID */

        /* Save A4 and set up MMU for new process */
        move.l  %a4, -(%sp)
        move.w  0x46(%a3), -(%sp)       /* Push ASID */
        jsr     MMU_$INSTALL_ASID
        addq.w  #2, %sp                 /* Pop ASID parameter */

        /* Clear MMU status register */
        move.b  #0, MMU_STATUS_REG      /* 0x00FFB403 */

        /* Set working set priority for new process */
        jsr     MMAP_$SET_WS_PRI

        /* Restore new process state and return */
        movea.l (%sp)+, %a1             /* A1 = new process PCB */
        movem.l 8(%a1), %d2-%d7/%a2-%a6/%sp  /* Restore registers */

dispatch_done:
        rts
