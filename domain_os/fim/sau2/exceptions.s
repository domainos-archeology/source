/*
 * fim/sau2/exceptions.s - FIM Exception Handlers (m68k/SAU2)
 *
 * Low-level CPU exception handlers for the m68010 processor.
 * These routines handle illegal instructions, privilege violations,
 * and other CPU exceptions.
 *
 * Original addresses:
 *   FIM_$EXIT:         0x00E228BC (2 bytes)
 *   FIM_$UII:          0x00E2146C (38 bytes)
 *   FIM_$PRIV_VIOL:    0x00E21530 (74 bytes)
 *   FIM_$ILLEGAL_USP:  0x00E2158A (4 bytes)
 */

        .text
        .even

/*
 * External references
 */
        .equ    STOP_WATCH_UII, 0x00E81A56      /* Stopwatch UII handler */
        .equ    FIM_COMMON_FAULT, 0x00E213A0    /* Common fault handler */

/*
 * FIM_$EXIT - Return from exception
 *
 * Simply executes the RTE instruction to return from an exception.
 * Used as the exit point from many exception handlers.
 *
 * Assembly (0x00E228BC):
 */
        .global FIM_$EXIT
FIM_$EXIT:
        rte

/*
 * FIM_$UII - Unimplemented Instruction Interrupt handler
 *
 * Handles line-A and line-F (unimplemented) instruction traps.
 * Checks if the instruction is a stopwatch instruction (0xA0xx or 0xA1xx)
 * and if so, dispatches to the stopwatch handler.
 *
 * Stack on entry (from exception):
 *   0(SP) - SR
 *   2(SP) - PC (high word)
 *   4(SP) - PC (low word) / Format
 *
 * Assembly (0x00E2146C):
 */
        .global FIM_$UII
FIM_$UII:
        btst.b  #5,(%sp)                /* Test supervisor bit in SR */
        beq.b   uii_fault               /* If user mode, it's a fault */
        move.l  %a0,-(%sp)              /* Save A0 */
        movea.l (0x6,%sp),%a0           /* A0 = faulting PC */
        cmpi.b  #0xA0,(%a0)             /* Check for 0xA0xx (line-A) */
        beq.b   uii_stopwatch           /* If stopwatch instruction */
        cmpi.b  #0xA1,(%a0)             /* Check for 0xA1xx */
        bne.b   uii_restore             /* Not stopwatch, fault */
uii_stopwatch:
        movea.l (%sp)+,%a0              /* Restore A0 */
        jmp     (STOP_WATCH_UII).l      /* Dispatch to stopwatch */
uii_restore:
        movea.l (%sp)+,%a0              /* Restore A0 */
uii_fault:
        jsr     (FIM_COMMON_FAULT).l    /* Call common fault handler */
        /* FIM_COMMON_FAULT does not return */

/*
 * FIM_$PRIV_VIOL - Privilege Violation handler
 *
 * Handles privilege violation exceptions. Special case: if the
 * instruction is "move SR,<ea>" (opcode 0x40Cx), we emulate it
 * by adjusting the PC past the instruction and returning.
 * This allows user-mode code to read the SR on 68010+.
 *
 * The "move from SR" instruction became privileged on 68010,
 * but was not privileged on 68000. This handler provides
 * backwards compatibility.
 *
 * Assembly (0x00E21530):
 */
        .global FIM_$PRIV_VIOL
FIM_$PRIV_VIOL:
        movem.l %d0/%d1/%d2/%a0/%a1,-(%sp) /* Save registers */
        movea.l (0x16,%sp),%a0          /* A0 = faulting PC */
        move.w  (%a0),%d0               /* D0 = faulting instruction */
        move.w  %d0,%d1                 /* Copy to D1 */
        and.w   #0xFFC0,%d1             /* Mask off EA bits */
        cmp.w   #0x40C0,%d1             /* Is it "move SR,<ea>"? */
        beq.b   priv_viol_move_sr       /* Yes, emulate it */
        movem.l (%sp)+,%d0/%d1/%d2/%a0/%a1 /* Restore registers */
        bra.b   priv_viol_fault         /* Call fault handler */

priv_viol_move_sr:
        /* Emulate "move SR,<ea>" by skipping past the instruction */
        move.w  %d0,%d1                 /* D1 = instruction */
        lsr.w   #3,%d1                  /* Shift to get EA mode */
        and.w   #0x7,%d1                /* Mask off mode bits */
        clr.l   %d2                     /* Clear D2 */
        /* Look up instruction length in table */
        move.b  (priv_viol_len_table,%pc,%d1:w),%d2
        cmp.w   #0x7,%d1                /* Is it mode 7? */
        bne.b   priv_viol_adjust        /* No, use table value */
        btst.l  #0,%d0                  /* Test register bit */
        beq.b   priv_viol_adjust        /* If 0, table is correct */
        addq.l  #2,%d2                  /* Mode 7, reg 1: add 2 */
priv_viol_adjust:
        add.l   %d2,(0x16,%sp)          /* Adjust PC past instruction */
        movem.l (%sp)+,%d0/%d1/%d2/%a0/%a1 /* Restore registers */
        jmp     FIM_$EXIT               /* Return from exception */

priv_viol_fault:
        jsr     (FIM_COMMON_FAULT).l    /* Call common fault handler */
        /* FIM_COMMON_FAULT does not return */

/*
 * Instruction length table for "move SR,<ea>" emulation
 * Indexed by EA mode (0-7)
 */
priv_viol_len_table:
        .byte   2                       /* Mode 0: Dn - 2 bytes */
        .byte   2                       /* Mode 1: An - 2 bytes (invalid) */
        .byte   2                       /* Mode 2: (An) - 2 bytes */
        .byte   2                       /* Mode 3: (An)+ - 2 bytes */
        .byte   2                       /* Mode 4: -(An) - 2 bytes */
        .byte   4                       /* Mode 5: d(An) - 4 bytes */
        .byte   4                       /* Mode 6: d(An,Xi) - 4 bytes */
        .byte   4                       /* Mode 7: abs/imm - 4+ bytes */
        .even

/*
 * FIM_$ILLEGAL_USP - Illegal User Stack Pointer handler
 *
 * Called when the user stack pointer is invalid.
 * Simply calls the common fault handler.
 *
 * Assembly (0x00E2158A):
 */
        .global FIM_$ILLEGAL_USP
FIM_$ILLEGAL_USP:
        jsr     (FIM_COMMON_FAULT).l    /* Call common fault handler */
        /* FIM_COMMON_FAULT does not return */

        .end
