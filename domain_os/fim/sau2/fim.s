/*
 * fim/sau2/fim.s - FIM Assembly Routines (m68k/SAU2)
 *
 * Combined assembly for the Fault/Interrupt Manager module.
 * All functions are ordered by their original ROM addresses to preserve
 * the memory layout, enabling PC-relative references between functions
 * and to nearby data.
 *
 * Original addresses (in ROM order):
 *   FIM_$CRASH:                0x00E1E864 (158 bytes)
 *   FIM_$UII:                  0x00E2146C (38 bytes)
 *   FIM_$GENERATE:             0x00E214A8 (6 bytes)
 *   FIM_$PRIV_VIOL:            0x00E21530 (74 bytes)
 *   FIM_$ILLEGAL_USP:          0x00E2158A (4 bytes)
 *   FIM_$CLEANUP:              0x00E21634 (40 bytes)
 *   FIM_$RLS_CLEANUP:          0x00E2165C (22 bytes)
 *   FIM_$POP_SIGNAL:           0x00E21672 (12 bytes)
 *   FIM_$SIGNAL_FIRST:         0x00E2167E (10 bytes)
 *   FIM_$SIGNAL:               0x00E21688 (42 bytes)
 *   cleanup_stack_table:       0x00E216B2 (256 bytes, data)
 *   FIM_$PROC2_STARTUP:        0x00E217B6 (30 bytes)
 *   FIM_$SINGLE_STEP:          0x00E217D4 (80 bytes)
 *   FIM_$FAULT_RETURN:         0x00E21828 (80 bytes)
 *   FIM_$SETUP_RETURN:         0x00E21878 (22 bytes)
 *   FP_$SAVEP:                 0x00E218D0 (4 bytes, data)
 *   FP_$OWNER:                 0x00E218D4 (2 bytes, data)
 *   fp_exclusion:              0x00E218D6 (4 bytes, data)
 *   FIM_$FLINE:                0x00E21ACC (68 bytes)
 *   FIM_$FP_ABORT:             0x00E21B80 (48 bytes; stub: 2 bytes)
 *   FIM_$FP_INIT:              0x00E21BB0 (84 bytes; stub: 2 bytes)
 *   FIM_$FSAVE:                0x00E21C34 (160 bytes; stub: 2 bytes)
 *   FIM_$FRESTORE:             0x00E21CD4 (116 bytes; stub: 2 bytes)
 *   FIM_$FP_GET_STATE:         0x00E21D48 (196 bytes; stub: 4 bytes)
 *   FIM_$FP_PUT_STATE:         0x00E21E0C (152 bytes; stub: 2 bytes)
 *   FIM_$SPURIOUS_INT:         0x00E21F20 (86 bytes)
 *   FIM_$PARITY_TRAP:          0x00E21F84 (98 bytes)
 *   FIM_$GET_USER_SR_PTR:      0x00E2277C (118 bytes)
 *   FIM_$DELIVER_TRACE_FAULT:  0x00E22866 (42 bytes)
 *   FIM_$CLEAR_TRACE_FAULT:    0x00E22890 (44 bytes)
 *   FIM_$EXIT:                 0x00E228BC (2 bytes)
 *
 * Note: FP stub functions provide no-op implementations for the
 * SAU2/68010 build which has no FPU.  For builds targeting 68020+
 * with 68881/68882, replace with full implementations.
 */

        .text
        .even

/* ====================================================================
 * External references - code and data outside this file
 * ==================================================================== */

        /* Process/scheduling data */
        .equ    PROC1_CURRENT,      0x00E20608  /* Current process ID (word) */
        .equ    PROC1_AS_ID,        0x00E2060A  /* Current address space ID */
        .equ    OS_STACK_BASE,      0x00E25C18  /* OS stack base table */

        /* FIM data (not yet emitted in this file) */
        .equ    FIM_QUIT_INH,       0x00E2248A  /* Quit inhibit array */
        .equ    FIM_TRACE_STS,      0x00E223A2  /* Trace status array */
        .equ    FIM_TRACE_BIT,      0x00E21888  /* Trace bit array */
        .equ    PENDING_TRACE,      0x00E21FF6  /* Pending trace faults */

        /* FIM code not yet in this file */
        .equ    FIM_COMMON_FAULT,   0x00E213A0  /* Common fault handler */
        .equ    FIM_BUILD_DF,       0x00E213A4  /* Build delivery frame */

        /* ML module */
        .equ    ML_EXCLUSION_START, 0x00E20DF8  /* Acquire exclusion lock */
        .equ    ML_EXCLUSION_STOP,  0x00E20E7E  /* Release exclusion lock */

        /* FP module */
        .equ    FP_SWITCH_OWNER_D2, 0x00E21B16  /* fp_$switch_owner+6: entry with D2 preloaded */

        /* Crash support */
        .equ    CRASH_SYSTEM,       0x00E1E700  /* System crash function */
        .equ    CRASH_PUTS,         0x00E1E7C8  /* Console output function */

        /* Hardware/peripheral */
        .equ    STOP_WATCH_UII,     0x00E81A56  /* Stopwatch UII handler */
        .equ    TIME_CLOCKH,        0x00E2B0D4  /* High word of system clock */
        .equ    CACHE_CLEAR,        0x00E242D4  /* Cache clear function */
        .equ    IO_HANDLER,         0x00E208FE  /* I/O interrupt handler */
        .equ    PARITY_CHK,         0x00E0AE68  /* Parity check function */

        /* Fault generation (not yet in this file) */
        .equ    GENERATE_TARGET,    0x00E21458  /* Fault generation entry */


/* ====================================================================
 * FIM_$CRASH - System crash handler
 *
 * Called when a fault cannot be delivered or is fatal.
 * Formats and displays crash information including:
 * - Exception type (SR format code)
 * - Fault address
 * - Saved registers
 *
 * Parameters:
 *   6(SP) - Pointer to exception frame
 *   A(SP) - Pointer to saved register area
 *   E(SP) - Pointer to status
 *
 * Stack on entry (after MOVE SR,-(SP)):
 *   0(SP) - Saved SR
 *   2(SP) - Return address (to caller)
 *   6(SP) - Exception frame pointer
 *   A(SP) - Register save area pointer
 *   E(SP) - Status pointer
 *
 * Assembly (0x00E1E864, 158 bytes):
 * ==================================================================== */
        .global FIM_$CRASH
FIM_$CRASH:
        move    %sr,-(%sp)              /* Save SR */
        ori     #0x0700,%sr             /* Disable interrupts */
        lea     (crash_data,%pc),%a5    /* A5 = crash data area */
        movea.l (0x6,%sp),%a2           /* A2 = exception frame */

        /* Save exception frame info */
        move.l  %a2,(crash_frame_ptr,%a5) /* Save frame pointer */
        move.w  (%a2),(crash_sr,%a5)    /* Save SR from frame */
        move.l  (0x2,%a2),(crash_pc,%a5) /* Save PC from frame */
        move.w  (0x6,%a2),%d0           /* D0 = format/vector word */
        move.w  %d0,(crash_format,%a5)  /* Save format word */

        /* Extract format code and look up exception type */
        lsr.w   #2,%d0                  /* D0 = format >> 2 */
        lea     (exc_type_table,%pc),%a0 /* A0 = type lookup table */

        /* Check if format code is in valid range */
        cmp.b   #0x20,%d0               /* Less than 0x20? */
        blt.b   crash_lookup_type       /* Yes, use lookup */
        cmp.b   #0x30,%d0               /* Less than 0x30? */
        blt.b   crash_use_table         /* Yes, use table directly */
crash_lookup_type:
        /* Scan exception type string for matching code */
        lea     (exc_type_chars,%pc),%a0
crash_scan:
        move.b  (%a0)+,%d1              /* Get type char */
        beq.b   crash_use_table         /* End of string, use default */
        cmp.b   %d0,%d1                 /* Match? */
        bne.b   crash_scan              /* No, try next */
crash_use_table:
        move.b  (%a0),(crash_type,%a5)  /* Store exception type char */

        /* Check if we need to extract additional fault info */
        moveq   #0x25,%d4               /* D4 = '%' (no additional info) */
        cmp.b   #0x03,%d0               /* Format < 4? */
        bgt.b   crash_no_extra          /* No extra info for format >= 4 */

        /* Extract fault address from short bus error frame */
        moveq   #0x20,%d4               /* D4 = ' ' (has additional info) */
        move.w  (0x8,%a2),%d5           /* D5 = instruction word */
        move.l  (0xA,%a2),%d6           /* D6 = fault address */

        /* Check frame format byte for format A/B distinction */
        cmpi.b  #0x80,(0x6,%a2)         /* Format 0x8x? */
        beq.b   crash_store_addr        /* Yes, use this address */

        /* Format B frame - different layout */
        move.w  (0xA,%a2),%d5           /* D5 = SSW */
        move.l  (0x10,%a2),%d6          /* D6 = fault address */
crash_store_addr:
        move.w  %d5,(crash_ssw,%a5)     /* Save SSW/instruction */
        move.l  %d6,(crash_fault_addr,%a5) /* Save fault address */

crash_no_extra:
        move.b  %d4,(crash_has_addr,%a5) /* Store has-address flag */

        /* Display crash message */
        lea     (crash_msg,%pc),%a0     /* A0 = message string */
        jsr     (CRASH_PUTS).l          /* Output to console */

        /* Set up for CRASH_SYSTEM call */
        movea.l (0xA,%sp),%a0           /* A0 = register save area */
        move.l  (0x3C,%a0),(crash_sp,%a5) /* Save SP from save area */

        /* Restore registers from save area and call CRASH_SYSTEM */
        move.l  (0x20,%a0),-(%sp)       /* Push additional context */
        movem.l (%a0)+,%d0-%d7/%a0-%a6  /* Restore all registers */
        movea.l (%sp)+,%a0              /* Pop to A0 */
        move.l  (0xE,%sp),-(%sp)        /* Push status pointer */
        jsr     (CRASH_SYSTEM).l        /* Call crash handler */
        addq.w  #4,%sp                  /* Clean up stack */

        move    (%sp)+,%sr              /* Restore SR */
        rts

/*
 * Crash data area
 */
crash_data:
crash_frame_ptr:
        .long   0                       /* Exception frame pointer */
crash_sr:
        .word   0                       /* Status register */
crash_pc:
        .long   0                       /* Program counter */
crash_format:
        .word   0                       /* Format/vector word */
crash_type:
        .byte   0                       /* Exception type character */
crash_has_addr:
        .byte   0x25                    /* '%' or ' ' */
crash_fault_addr:
        .long   0                       /* Fault address */
crash_ssw:
        .word   0                       /* Special status word */
crash_sp:
        .long   0                       /* Stack pointer */
        .even

/*
 * Exception type character lookup table
 * Maps format codes to display characters
 */
exc_type_chars:
        .ascii  "S0O1O2O3O4O5O6O"       /* Exception type codes */
        .byte   0
        .even

/*
 * Exception type table
 * Direct lookup for format codes >= 0x20
 */
exc_type_table:
        .ascii  "ABCDEFGHIJKLMNOP"
        .byte   0
        .even

/*
 * Crash message string
 */
crash_msg:
        .ascii  "FAULT IN DOMAIN_OS "
        .byte   0
        .even


/* ====================================================================
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
 * Assembly (0x00E2146C, 38 bytes):
 * ==================================================================== */
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


/* ====================================================================
 * FIM_$GENERATE - Generate a fault/signal
 *
 * Small stub that pops return address, gets status, and
 * branches to the common fault generation code.
 *
 * Parameters:
 *   4(SP) - Status code
 *
 * Assembly (0x00E214A8, 6 bytes):
 * ==================================================================== */
        .global FIM_$GENERATE
FIM_$GENERATE:
        addq.w  #4,%sp                  /* Pop return address */
        move.l  (%sp)+,%d0              /* D0 = status code */
        bra.w   generate_common         /* Branch to common code */

/*
 * Common fault generation code
 * Branches to the main fault handler setup at 0x00E21458
 */
generate_common:
        jmp     (GENERATE_TARGET).l


/* ====================================================================
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
 * Assembly (0x00E21530, 74 bytes):
 * ==================================================================== */
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


/* ====================================================================
 * FIM_$ILLEGAL_USP - Illegal User Stack Pointer handler
 *
 * Called when the user stack pointer is invalid.
 * Simply calls the common fault handler.
 *
 * Assembly (0x00E2158A, 4 bytes):
 * ==================================================================== */
        .global FIM_$ILLEGAL_USP
FIM_$ILLEGAL_USP:
        jsr     (FIM_COMMON_FAULT).l    /* Call common fault handler */
        /* FIM_COMMON_FAULT does not return */


/* ====================================================================
 * FIM_$CLEANUP - Establish a cleanup handler
 *
 * Similar to setjmp - saves current context and pushes handler
 * onto the per-process cleanup handler stack.
 *
 * Parameters:
 *   4(SP) - Pointer to handler context buffer (5 longs)
 *
 * Handler context layout:
 *   0x00: Previous handler pointer (link)
 *   0x04: Return address
 *   0x08: Saved A5
 *   0x0C: Saved A6
 *   0x10: Saved SP
 *
 * Returns:
 *   D0 = status_$cleanup_handler_set (0x00120035)
 *
 * Assembly (0x00E21634, 40 bytes):
 * ==================================================================== */
        .global FIM_$CLEANUP
FIM_$CLEANUP:
        move.w  (PROC1_CURRENT).l,%d0   /* D0 = PROC1_$CURRENT */
        movea.l (0x4,%sp),%a1           /* A1 = handler context ptr */
        lsl.w   #2,%d0                  /* D0 = process * 4 (index) */
        lea     (cleanup_stack_table,%pc),%a0 /* A0 = &cleanup_stack[0] */
        adda.w  %d0,%a0                 /* A0 = &cleanup_stack[process] */
        move.l  (%sp),%d1               /* D1 = return address */
        move.l  (%a0),%d0               /* D0 = current handler (link) */
        movea.l (0x4,%sp),%a1           /* A1 = handler context ptr */
        movem.l %d0/%d1/%a5/%a6/%sp,(%a1) /* Save link, ret, A5, A6, SP */
        move.l  %a1,(%a0)               /* Push handler onto stack */
        move.l  #0x00120035,%d0         /* status_$cleanup_handler_set */
        rts


/* ====================================================================
 * FIM_$RLS_CLEANUP - Release (pop) cleanup handler
 *
 * Removes the most recently established cleanup handler.
 * Called when exiting the scope that established the handler.
 *
 * Parameters:
 *   4(SP) - Pointer to handler context buffer
 *
 * Assembly (0x00E2165C, 22 bytes):
 * ==================================================================== */
        .global FIM_$RLS_CLEANUP
FIM_$RLS_CLEANUP:
        move.w  (PROC1_CURRENT).l,%d0   /* D0 = PROC1_$CURRENT */
        movea.l (0x4,%sp),%a1           /* A1 = handler context ptr */
        lsl.w   #2,%d0                  /* D0 = process * 4 */
        lea     (cleanup_stack_table,%pc),%a0 /* A0 = &cleanup_stack[0] */
        adda.w  %d0,%a0                 /* A0 = &cleanup_stack[process] */
        move.l  (%a1),(%a0)             /* Pop: stack = handler->link */
        rts


/* ====================================================================
 * FIM_$POP_SIGNAL - Pop handler and continue
 *
 * Used to pop a handler after cleanup without restoring context.
 * Adjusts SP from the saved handler and jumps to return address.
 *
 * Assembly (0x00E21672, 12 bytes):
 * ==================================================================== */
        .global FIM_$POP_SIGNAL
FIM_$POP_SIGNAL:
        movea.l (%sp)+,%a0              /* A0 = caller return address */
        movea.l (%sp),%a1               /* A1 = handler context ptr */
        movea.l (0x10,%a1),%sp          /* SP = handler->saved_sp */
        addq.w  #4,%sp                  /* Skip over our return addr */
        jmp     (%a0)                   /* Return to caller */


/* ====================================================================
 * FIM_$SIGNAL_FIRST - Signal first handler (entry point)
 *
 * Entry point that preserves A5/A6 before falling through to FIM_$SIGNAL.
 *
 * Parameters:
 *   4(SP) - Status code to deliver
 *
 * Assembly (0x00E2167E, 10 bytes):
 * ==================================================================== */
        .global FIM_$SIGNAL_FIRST
FIM_$SIGNAL_FIRST:
        move.l  (0x4,%sp),%d0           /* D0 = status code */
        movem.l %a5/%a6,-(%sp)          /* Save A5, A6 */
        bra.b   signal_common           /* Fall through to FIM_$SIGNAL */


/* ====================================================================
 * FIM_$SIGNAL - Signal cleanup handlers (longjmp-like)
 *
 * Invokes the cleanup handler chain. If a handler is established,
 * this function does not return - it restores the handler's context
 * and jumps to the return address saved by FIM_$CLEANUP.
 *
 * If no handler is established, returns normally.
 *
 * Parameters:
 *   4(SP) - Status code to deliver (returned in D0)
 *
 * Assembly (0x00E21688, 42 bytes):
 * ==================================================================== */
        .global FIM_$SIGNAL
FIM_$SIGNAL:
        addq.l  #4,%sp                  /* Pop return address */
        move.l  (%sp)+,%d0              /* D0 = status code */
signal_common:
        move.w  (PROC1_CURRENT).l,%d1   /* D1 = PROC1_$CURRENT */
        lea     (cleanup_stack_table,%pc),%a0 /* A0 = &cleanup_stack[0] */
        lsl.w   #2,%d1                  /* D1 = process * 4 */
        adda.w  %d1,%a0                 /* A0 = &cleanup_stack[process] */
        move.l  (%a0),%d1               /* D1 = current handler */
        beq.b   no_handler              /* If NULL, no handler */
        movea.l %d1,%a1                 /* A1 = handler context */
        move.l  (%a1)+,(%a0)            /* Pop handler: stack = link */
        movea.l (%a1)+,%a0              /* A0 = saved return address */
        movea.l (%a1)+,%a5              /* Restore A5 */
        movea.l (%a1)+,%a6              /* Restore A6 */
        subq.l  #4,%sp                  /* Make room for return */
        jmp     (%a0)                   /* Jump to saved address */
no_handler:
        movem.l (%sp)+,%a5/%a6          /* Restore A5, A6 */
        rts

/*
 * Cleanup handler stack table
 * One entry per process (indexed by PROC1_$CURRENT << 2)
 * Each entry is a pointer to the head of the cleanup handler list
 *
 * Address: 0x00E216B2
 */
cleanup_stack_table:
        .space  256                     /* 64 processes * 4 bytes each */


/* ====================================================================
 * FIM_$PROC2_STARTUP - New process startup entry
 *
 * Entry point for a newly forked process. Sets up the initial
 * user-mode state and returns via RTE.
 *
 * Parameters:
 *   4(SP) - Pointer to startup context
 *           [0] = Initial USP
 *           [4] = Initial PC
 *
 * Assembly (0x00E217B6, 30 bytes):
 * ==================================================================== */
        .global FIM_$PROC2_STARTUP
FIM_$PROC2_STARTUP:
        movea.l (0x4,%sp),%a5           /* A5 = startup context */
        bsr.w   FIM_$SETUP_RETURN       /* Set up return frame */
        clr.w   (%a0)                   /* Clear format word */
        move.l  (0x4,%a5),-(%a0)        /* Push PC */
        clr.w   -(%a0)                  /* Push SR = 0 (user mode) */
        movea.l (%a5),%a1               /* A1 = initial USP */
        move    %a1,%usp                /* Set user SP */
        movea.l %a0,%sp                 /* Switch to exception frame */
        suba.l  %a6,%a6                 /* Clear A6 (frame pointer) */
        jmp     FIM_$EXIT               /* Return to user mode */


/* ====================================================================
 * FIM_$SINGLE_STEP - Single step exception handler
 *
 * Handles trace exceptions for single-step debugging.
 * Sets up trace status and returns to the traced instruction.
 *
 * Parameters:
 *   4(SP) - Pointer to PC storage
 *   8(SP) - Pointer to SR storage
 *   C(SP) - Pointer to register save area
 *
 * Assembly (0x00E217D4, 80 bytes):
 * ==================================================================== */
        .global FIM_$SINGLE_STEP
FIM_$SINGLE_STEP:
        move.w  (PROC1_AS_ID).l,%d0     /* D0 = AS ID */
        lea     (FIM_QUIT_INH).l,%a0    /* A0 = quit inhibit table */
        st      (0,%a0,%d0:w)           /* Set quit inhibited */
        lsl.w   #2,%d0                  /* D0 = AS * 4 */
        lea     (FIM_TRACE_STS).l,%a0   /* A0 = trace status table */
        move.l  #0x00120015,(0,%a0,%d0:w) /* Set trace fault status */
        bsr.w   FIM_$SETUP_RETURN       /* Set up return frame */
        movea.l (0x4,%sp),%a1           /* A1 = PC ptr */
        move.l  (%a1),-(%a0)            /* Push PC */
        movea.l (0x8,%sp),%a1           /* A1 = SR ptr */
        move.w  (%a1),%d0               /* D0 = SR */
        and.w   #0xC01F,%d0             /* Keep only relevant bits */
        bset.l  #15,%d0                 /* Set trace bit */
        move.w  %d0,-(%a0)              /* Push modified SR */
        movea.l (0xC,%sp),%a1           /* A1 = regs ptr */
        movea.l (0x3C,%a1),%a2          /* A2 = saved USP */
        move    %a2,%usp                /* Restore USP */
        movea.l %a0,%sp                 /* Switch to exception frame */
        movem.l (%a1),%d0-%d7/%a0-%a6   /* Restore registers */
        jmp     FIM_$EXIT               /* Return from exception */


/* ====================================================================
 * FIM_$FAULT_RETURN - Return from fault handler
 *
 * Restores state after a fault has been handled by user code.
 * Optionally restores FP state.
 *
 * Parameters:
 *   4(SP) - Pointer to saved context ptr
 *   8(SP) - Pointer to register restore ptr
 *   C(SP) - Pointer to FP state ptr
 *
 * Assembly (0x00E21828, 80 bytes):
 * ==================================================================== */
        .global FIM_$FAULT_RETURN
FIM_$FAULT_RETURN:
        movea.l (0x4,%sp),%a2           /* A2 = context ptr ptr */
        movea.l (0x8,%sp),%a3           /* A3 = regs ptr ptr */
        movea.l (0xC,%sp),%a0           /* A0 = FP state ptr ptr */
        tst.l   (%a0)                   /* Check if FP state exists */
        beq.b   fault_ret_setup         /* No FP state */
        move.l  %a0,-(%sp)              /* Push FP state ptr */
        bsr.w   FIM_$FRESTORE           /* Restore FP state */
fault_ret_setup:
        bsr.w   FIM_$SETUP_RETURN       /* Set up return frame */
        clr.w   (%a0)                   /* Clear format word */
        movea.l (%a2),%a2               /* A2 = context ptr */
        move.l  (0x14,%a2),-(%a0)       /* Push PC */
        move.l  (0x18,%a2),%d0          /* D0 = saved SR */
        and.w   #0xC01F,%d0             /* Keep only relevant bits */
        move.w  %d0,-(%a0)              /* Push SR */
        move.l  (0xC,%a2),-(%a0)        /* Push additional context */
        move.l  (0x10,%a2),-(%a0)       /* Push more context */
        movea.l (0x8,%a2),%a1           /* A1 = saved USP */
        move    %a1,%usp                /* Restore USP */
        movea.l %a0,%sp                 /* Switch to exception frame */
        tst.l   (%a3)                   /* Check if regs to restore */
        beq.b   fault_ret_exit          /* No regs */
        movea.l (%a3),%a3               /* A3 = regs ptr */
        movem.l (%a3),%d0-%d7/%a0-%a6   /* Restore all regs */
fault_ret_exit:
        movea.l (%sp)+,%a5              /* Restore A5 */
        movea.l (%sp)+,%a6              /* Restore A6 */
        jmp     FIM_$EXIT               /* Return from exception */


/* ====================================================================
 * FIM_$SETUP_RETURN - Set up OS stack return frame
 *
 * Returns the OS stack pointer for the current process in A0,
 * adjusted past the format word.  Used by PROC2_STARTUP,
 * SINGLE_STEP, and FAULT_RETURN before building an exception frame.
 *
 * On entry:  (no parameters)
 * On exit:   A0 = OS stack top - 2  (room for format word already allocated)
 *
 * Assembly (0x00E21878, 22 bytes):
 * ==================================================================== */
        .global FIM_$SETUP_RETURN
FIM_$SETUP_RETURN:
        move.w  (PROC1_CURRENT).l,%d0   /* D0 = current process ID */
        lsl.w   #2,%d0                  /* D0 *= 4 (index into table) */
        lea     (OS_STACK_BASE).l,%a0   /* A0 = stack base table */
        movea.l (0,%a0,%d0:w),%a0       /* A0 = this process's stack top */
        subq.l  #2,%a0                  /* Point past format word */
        rts


/* ====================================================================
 * FP module data (PC-relative from FIM_$FLINE)
 *
 * In the original ROM, these were located at:
 *   FP_$SAVEP    0x00E218D0 - uint32: non-zero if FPU hardware is present
 *   FP_$OWNER    0x00E218D4 - uint16: AS ID of current FPU owner
 *   fp_exclusion 0x00E218D6 - ml_$exclusion_t: FP exclusion lock
 *
 * Accessed PC-relative by FIM_$FLINE.  Also accessed by other FP
 * routines (fp_$switch_owner, FIM_$FP_INIT, etc.) via global labels.
 * ==================================================================== */
        .global FP_$SAVEP
FP_$SAVEP:
        .long   0                       /* Non-zero if FPU present */

        .global FP_$OWNER
FP_$OWNER:
        .word   0                       /* AS ID of current FPU owner */

        .global fp_exclusion
fp_exclusion:
        .long   0                       /* ml_$exclusion_t */


/* ====================================================================
 * FIM_$FLINE - F-Line (coprocessor) exception handler
 *
 * Handles F-Line traps for lazy FPU context switching.
 * When a process executes an FPU instruction without owning the FPU,
 * saves the previous owner's FP state, restores the new owner's,
 * and retries the instruction.  If no FPU is present or the current
 * process already owns the FPU, forwards to FIM_$UII.
 *
 * This is the handler for exception vector 0x2C (F-Line emulator).
 *
 * Registers saved/restored: D0-D3, A0-A1
 *
 * Original encoding notes:
 *   - FP_$SAVEP, FP_$OWNER, fp_exclusion accessed PC-relative (above).
 *   - fp_$switch_owner+6 was called via bsr.b in ROM; here we use jsr
 *     to the external symbol since fp_$switch_owner is in fp/sau2/.
 *   - FIM_$EXIT reached via jmp PC-relative (now a local label).
 *   - FIM_$UII reached via jmp absolute in ROM (now a local label).
 *
 * Assembly (0x00E21ACC, 68 bytes):
 * ==================================================================== */
        .global FIM_$FLINE
FIM_$FLINE:
        movem.l %d0-%d3/%a0-%a1,-(%sp)         /* Save registers */
        move.l  (FP_$SAVEP,%pc),%d0             /* d0 = FP_$SAVEP */
        beq.b   .fline_no_fpu                   /* No FPU -> UII */
        move.w  (PROC1_AS_ID).l,%d2             /* d2 = current AS ID */
        beq.b   .fline_no_fpu                   /* AS 0 -> UII */
        cmp.w   (FP_$OWNER,%pc),%d2             /* Compare with FP owner */
        beq.b   .fline_no_fpu                   /* Same owner -> UII */
        pea     (fp_exclusion,%pc)              /* Push exclusion lock ptr */
        jsr     (ML_EXCLUSION_START).l          /* Acquire exclusion */
        addq.l  #4,%sp
        jsr     (FP_SWITCH_OWNER_D2).l          /* Switch FP context (d2 has AS ID) */
        pea     (fp_exclusion,%pc)              /* Push exclusion lock ptr */
        jsr     (ML_EXCLUSION_STOP).l           /* Release exclusion */
        addq.l  #4,%sp
        movem.l (%sp)+,%d0-%d3/%a0-%a1          /* Restore registers */
        jmp     FIM_$EXIT                       /* RTE - retry FPU instruction */
.fline_no_fpu:
        movem.l (%sp)+,%d0-%d3/%a0-%a1          /* Restore registers */
        jmp     FIM_$UII                        /* Forward to UII handler */


/* ====================================================================
 * FIM_$FP_ABORT - Abort floating point operation (stub)
 *
 * On 68010 without FPU, this is a no-op.
 * Full implementation: 0x00E21B80 (48 bytes)
 * ==================================================================== */
        .global FIM_$FP_ABORT
FIM_$FP_ABORT:
        rts


/* ====================================================================
 * FIM_$FP_INIT - Initialize FPU state (stub)
 *
 * On 68010 without FPU, this is a no-op.
 * Full implementation: 0x00E21BB0 (84 bytes)
 * ==================================================================== */
        .global FIM_$FP_INIT
FIM_$FP_INIT:
        rts


/* ====================================================================
 * FIM_$FSAVE - Save FPU state (stub)
 *
 * On 68010 without FPU, this is a no-op.
 * Full implementation: 0x00E21C34 (160 bytes)
 * ==================================================================== */
        .global FIM_$FSAVE
FIM_$FSAVE:
        rts


/* ====================================================================
 * FIM_$FRESTORE - Restore FPU state (stub)
 *
 * On 68010 without FPU, this is a no-op.
 * Full implementation: 0x00E21CD4 (116 bytes)
 * ==================================================================== */
        .global FIM_$FRESTORE
FIM_$FRESTORE:
        rts


/* ====================================================================
 * FIM_$FP_GET_STATE - Get FPU state (stub)
 *
 * On 68010 without FPU, returns -1 (not available).
 * Full implementation: 0x00E21D48 (196 bytes)
 * ==================================================================== */
        .global FIM_$FP_GET_STATE
FIM_$FP_GET_STATE:
        moveq   #-1,%d0                 /* Return -1 (not available) */
        rts


/* ====================================================================
 * FIM_$FP_PUT_STATE - Put FPU state (stub)
 *
 * On 68010 without FPU, this is a no-op.
 * Full implementation: 0x00E21E0C (152 bytes)
 * ==================================================================== */
        .global FIM_$FP_PUT_STATE
FIM_$FP_PUT_STATE:
        rts


/* ====================================================================
 * FIM_$SPURIOUS_INT - Spurious interrupt handler
 *
 * Handles spurious interrupts (no device acknowledged).
 * Counts spurious interrupts and crashes if too many
 * occur in one clock tick.
 *
 * Assembly (0x00E21F20, 86 bytes):
 * ==================================================================== */
        .global FIM_$SPURIOUS_INT
FIM_$SPURIOUS_INT:
        movem.l %d0/%a5,-(%sp)          /* Save registers */
        lea     (spur_data,%pc),%a5     /* A5 = local data */
        addq.l  #1,(spur_count,%a5)     /* Increment count */
        move.l  (TIME_CLOCKH).l,%d0     /* D0 = current clock */
        cmp.l   (spur_last_clock,%a5),%d0 /* Same tick? */
        bne.b   spur_new_tick           /* No, reset counter */
        subq.w  #1,(spur_tick_cnt,%a5)  /* Decrement tick count */
        bne.b   spur_done               /* Not zero, continue */
        /* Too many spurious interrupts - crash */
        movem.l (%sp)+,%d0/%a5          /* Restore */
        movem.l %d0-%d7/%a0-%a6/%sp,-(%sp) /* Save all for crash */
        pea     (spur_regs,%pc)         /* Push regs ptr */
        pea     (0x4,%sp)               /* Push D0/D1 ptr */
        pea     (0x48,%sp)              /* Push exception frame */
        bsr.w   FIM_$CRASH              /* Crash */
        adda.w  #12,%sp                 /* Clean up */
        movem.l (%sp)+,%d0-%d7/%a0-%a6  /* Restore */
        addq.w  #4,%sp                  /* Pop SP save */
        rte                             /* Return */
spur_new_tick:
        move.w  #1000,(spur_tick_cnt,%a5) /* Reset count to 1000 */
        move.l  %d0,(spur_last_clock,%a5) /* Save current clock */
spur_done:
        movem.l (%sp)+,%d0/%a5          /* Restore */
        jmp     FIM_$EXIT               /* Return from exception */

/*
 * Local data for spurious interrupt handler
 */
spur_data:
spur_count:
        .long   0                       /* Total spurious count */
spur_last_clock:
        .long   0                       /* Last clock tick seen */
spur_tick_cnt:
        .word   1000                    /* Count per tick */
spur_regs:
        .space  64                      /* Register save area */


/* ====================================================================
 * FIM_$PARITY_TRAP - Parity error trap handler
 *
 * Handles memory parity errors. Checks if the error is
 * recoverable and either continues or crashes.
 *
 * Assembly (0x00E21F84, 98 bytes):
 * ==================================================================== */
        .global FIM_$PARITY_TRAP
FIM_$PARITY_TRAP:
        movem.l %d0/%d1/%a0/%a1,-(%sp)  /* Save registers */
        jsr     (PARITY_CHK).l          /* Check parity status */
        lea     (parity_data,%pc),%a0   /* A0 = local data */
        clr.w   (%a0)                   /* Clear status */
        tst.b   %d0                     /* Check result */
        bmi.b   parity_error            /* Negative = error */
        jmp     (IO_HANDLER).l          /* Handle as I/O int */
parity_error:
        /* Parity error - try to deliver as fault */
        movem.l (%sp)+,%d0/%d1/%a0/%a1  /* Restore */
        suba.w  #0x3A,%sp               /* Make room for frame */
        move.w  (0x3A,%sp),(%sp)        /* Copy SR */
        move.l  (0x3C,%sp),(0x2,%sp)    /* Copy PC */
        move.b  #0x80,(0x6,%sp)         /* Set frame type */
        /* Set up parameters and call BUILD_DF */
        move.w  #0x3000,-(%sp)          /* Push flags */
        move.w  #0x13,-(%sp)            /* Push vector */
        move.l  #0x0012001E,-(%sp)      /* Push status */
        andi    #0xF8FF,%sr             /* Enable interrupts */
        pea     (%sp)                   /* Push frame ptr */
        pea     (0xC,%sp)               /* Push regs ptr */
        jmp     (FIM_BUILD_DF).l        /* Build delivery frame */

parity_data:
        .word   0                       /* Parity status */
        .long   0                       /* Fault address */
        .word   0                       /* Additional info */


/* ====================================================================
 * FIM_$GET_USER_SR_PTR - Get pointer to user SR
 *
 * Scans the exception frame chain to find the SR word
 * for the user-mode context.
 *
 * Parameters:
 *   4(SP) - Process ID
 *
 * Returns:
 *   D0 = Pointer to SR word
 *
 * Assembly (0x00E2277C, 118 bytes):
 * ==================================================================== */
        .global FIM_$GET_USER_SR_PTR
FIM_$GET_USER_SR_PTR:
        move.w  (0x4,%sp),%d0           /* D0 = process ID */
        lsl.w   #2,%d0                  /* D0 = process * 4 */
        lea     (OS_STACK_BASE).l,%a0   /* A0 = stack base table */
        move.l  (0,%a0,%d0:w),%d0       /* D0 = stack base */
        subq.l  #8,%d0                  /* Point to top of frame */
        movea.l %d0,%a0                 /* A0 = frame ptr */
        clr.w   %d1                     /* D1 = frame type 0 */
        bsr.b   check_frame_type
        beq.b   found_user_sr
        movea.l %a0,%a1                 /* Save frame ptr */
        lea     (-0x32,%a1),%a0         /* Try format A frame */
        move.w  #0x8008,%d1             /* Type A frame */
        bsr.b   check_frame_type
        beq.b   found_user_sr
        move.w  #0x800C,%d1             /* Type B frame */
        bsr.b   check_frame_type
        beq.b   found_user_sr
        lea     (-0x54,%a1),%a0         /* Try format B frame */
        move.w  #0xB008,%d1
        bsr.b   check_frame_type
        beq.b   found_user_sr
        move.w  #0xB00C,%d1
        bsr.b   check_frame_type
        beq.b   found_user_sr
        lea     (-0x18,%a1),%a0         /* Try format 9 frame */
        move.w  #0xA008,%d1
        bsr.b   check_frame_type
        beq.b   found_user_sr
        move.w  #0xA00C,%d1
        bsr.b   check_frame_type
        beq.b   found_user_sr
        lea     (-0x4,%a1),%a0          /* Try short frame */
        move.w  #0x2000,%d1
        bsr.b   check_frame_type
        beq.b   found_user_sr
        lea     (-0xC,%a1),%a0          /* Try throwaway frame */
        move.w  #0x9000,%d1
        bsr.b   check_frame_type
        beq.b   found_user_sr
        movea.l %a1,%a0                 /* Return original */
found_user_sr:
        move.l  %a0,%d0                 /* Return ptr in D0 */
        rts

/*
 * Check frame type helper
 * Compares frame type at A0 with expected type in D1
 */
check_frame_type:
        /* Internal subroutine to check exception frame format */
        /* Sets Z flag if match */
        rts


/* ====================================================================
 * FIM_$DELIVER_TRACE_FAULT - Mark process for trace fault
 *
 * Sets the trace bit for a process and increments the
 * pending trace fault count.
 *
 * Parameters:
 *   4(SP) - Process/AS ID
 *
 * Assembly (0x00E22866, 42 bytes):
 * ==================================================================== */
        .global FIM_$DELIVER_TRACE_FAULT
FIM_$DELIVER_TRACE_FAULT:
        lea     (FIM_TRACE_BIT).l,%a1   /* A1 = trace bit table */
        move.w  (0x4,%sp),%d0           /* D0 = process ID */
        move    %sr,-(%sp)              /* Save SR */
        ori     #0x0700,%sr             /* Disable interrupts */
        bset.b  #7,(0,%a1,%d0:w)        /* Set trace bit */
        bne.b   trace_already_set       /* Already set, skip */
        addq.l  #1,(PENDING_TRACE).l    /* Increment pending count */
        /* Patch FIM_$EXIT to NOP to catch trace */
        move.w  #0x4E71,(FIM_$EXIT).l   /* Write NOP instruction */
        jsr     (CACHE_CLEAR).l         /* Clear instruction cache */
trace_already_set:
        move    (%sp)+,%sr              /* Restore SR */
        rts


/* ====================================================================
 * FIM_$CLEAR_TRACE_FAULT - Clear trace fault for process
 *
 * Clears the trace bit for a process and decrements the
 * pending trace fault count.
 *
 * Parameters:
 *   4(SP) - Process/AS ID
 *
 * Assembly (0x00E22890, 44 bytes):
 * ==================================================================== */
        .global FIM_$CLEAR_TRACE_FAULT
FIM_$CLEAR_TRACE_FAULT:
        lea     (FIM_TRACE_BIT).l,%a1   /* A1 = trace bit table */
        move.w  (0x4,%sp),%d0           /* D0 = process ID */
        ori     #0x0700,%sr             /* Disable interrupts */
        bclr.b  #7,(0,%a1,%d0:w)        /* Clear trace bit */
        beq.b   trace_not_set           /* Wasn't set, skip */
        subq.l  #1,(PENDING_TRACE).l    /* Decrement pending count */
        bne.b   trace_not_set           /* Still others pending */
        /* Restore FIM_$EXIT to RTE */
        move.w  #0x4E73,(FIM_$EXIT).l   /* Write RTE instruction */
        jsr     (CACHE_CLEAR).l         /* Clear instruction cache */
trace_not_set:
        andi    #0xF8FF,%sr             /* Re-enable interrupts */
        rts


/* ====================================================================
 * FIM_$EXIT - Return from exception
 *
 * Simply executes the RTE instruction to return from an exception.
 * Used as the exit point from many exception handlers.
 *
 * Note: This location is self-modified by FIM_$DELIVER_TRACE_FAULT
 * (patched to NOP) and FIM_$CLEAR_TRACE_FAULT (restored to RTE)
 * to intercept returns for trace fault delivery.
 *
 * Assembly (0x00E228BC, 2 bytes):
 * ==================================================================== */
        .global FIM_$EXIT
FIM_$EXIT:
        rte

        .end
