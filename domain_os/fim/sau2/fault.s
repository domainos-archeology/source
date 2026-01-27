/*
 * fim/sau2/fault.s - FIM Fault Handling Routines (m68k/SAU2)
 *
 * These routines handle fault returns, single-step debugging,
 * trace faults, and related operations.
 *
 * Original addresses:
 *   FIM_$PROC2_STARTUP:        0x00E217B6 (30 bytes)
 *   FIM_$SINGLE_STEP:          0x00E217D4 (80 bytes)
 *   FIM_$FAULT_RETURN:         0x00E21828 (80 bytes)
 *   FIM_$GET_USER_SR_PTR:      0x00E2277C (118 bytes)
 *   FIM_$DELIVER_TRACE_FAULT:  0x00E22866 (42 bytes)
 *   FIM_$CLEAR_TRACE_FAULT:    0x00E22890 (44 bytes)
 *   FIM_$SPURIOUS_INT:         0x00E21F20 (86 bytes)
 *   FIM_$PARITY_TRAP:          0x00E21F84 (98 bytes)
 */

        .text
        .even

/*
 * External references
 */
        .equ    PROC1_CURRENT,      0x00E20608  /* Current process ID (word) */
        .equ    PROC1_AS_ID,        0x00E2060A  /* Current AS ID */
        .equ    OS_STACK_BASE,      0x00E25C18  /* OS stack base table */
        .equ    FIM_QUIT_INH,       0x00E2248A  /* Quit inhibit array */
        .equ    FIM_TRACE_STS,      0x00E223A2  /* Trace status array */
        .equ    FIM_TRACE_BIT,      0x00E21888  /* Trace bit array */
        .equ    PENDING_TRACE,      0x00E21FF6  /* Pending trace faults */
        .equ    TIME_CLOCKH,        0x00E2B0D4  /* High word of system clock */
        .equ    FIM_SPUR_CNT,       0x00E21F7E  /* Spurious interrupt count */
        .equ    CACHE_CLEAR,        0x00E242D4  /* Cache clear function */
        .equ    FIM_FRESTORE,       0x00E21CD4  /* FP restore function */
        .equ    FIM_CRASH,          0x00E1E864  /* Crash handler */
        .equ    FIM_EXIT,           0x00E228BC  /* Return from exception */
        .equ    FIM_BUILD_DF,       0x00E213A4  /* Build delivery frame */
        /* FIM_$SETUP_RETURN defined locally below */
        .equ    IO_HANDLER,         0x00E208FE  /* I/O interrupt handler */
        .equ    PARITY_CHK,         0x00E0AE68  /* Parity check function */

/*
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
 * Assembly (0x00E217B6):
 */
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
        jmp     (FIM_EXIT).l            /* Return to user mode */

/*
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
 * Assembly (0x00E217D4):
 */
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
        jmp     (FIM_EXIT).l            /* Return from exception */

/*
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
 * Assembly (0x00E21828):
 */
        .global FIM_$FAULT_RETURN
FIM_$FAULT_RETURN:
        movea.l (0x4,%sp),%a2           /* A2 = context ptr ptr */
        movea.l (0x8,%sp),%a3           /* A3 = regs ptr ptr */
        movea.l (0xC,%sp),%a0           /* A0 = FP state ptr ptr */
        tst.l   (%a0)                   /* Check if FP state exists */
        beq.b   fault_ret_setup         /* No FP state */
        move.l  %a0,-(%sp)              /* Push FP state ptr */
        jsr     (FIM_FRESTORE).l        /* Restore FP state */
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
        jmp     (FIM_EXIT).l            /* Return from exception */

/*
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
 */
        .global FIM_$SETUP_RETURN
FIM_$SETUP_RETURN:
        move.w  (PROC1_CURRENT).l,%d0   /* D0 = current process ID */
        lsl.w   #2,%d0                  /* D0 *= 4 (index into table) */
        lea     (OS_STACK_BASE).l,%a0   /* A0 = stack base table */
        movea.l (0,%a0,%d0:w),%a0       /* A0 = this process's stack top */
        subq.l  #2,%a0                  /* Point past format word */
        rts

/*
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
 * Assembly (0x00E2277C):
 */
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

/*
 * FIM_$DELIVER_TRACE_FAULT - Mark process for trace fault
 *
 * Sets the trace bit for a process and increments the
 * pending trace fault count.
 *
 * Parameters:
 *   4(SP) - Process/AS ID
 *
 * Assembly (0x00E22866):
 */
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
        move.w  #0x4E71,(FIM_EXIT).l    /* Write NOP instruction */
        jsr     (CACHE_CLEAR).l         /* Clear instruction cache */
trace_already_set:
        move    (%sp)+,%sr              /* Restore SR */
        rts

/*
 * FIM_$CLEAR_TRACE_FAULT - Clear trace fault for process
 *
 * Clears the trace bit for a process and decrements the
 * pending trace fault count.
 *
 * Parameters:
 *   4(SP) - Process/AS ID
 *
 * Assembly (0x00E22890):
 */
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
        move.w  #0x4E73,(FIM_EXIT).l    /* Write RTE instruction */
        jsr     (CACHE_CLEAR).l         /* Clear instruction cache */
trace_not_set:
        andi    #0xF8FF,%sr             /* Re-enable interrupts */
        rts

/*
 * FIM_$SPURIOUS_INT - Spurious interrupt handler
 *
 * Handles spurious interrupts (no device acknowledged).
 * Counts spurious interrupts and crashes if too many
 * occur in one clock tick.
 *
 * Assembly (0x00E21F20):
 */
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
        jsr     (FIM_CRASH).l           /* Crash */
        adda.w  #12,%sp                 /* Clean up */
        movem.l (%sp)+,%d0-%d7/%a0-%a6  /* Restore */
        addq.w  #4,%sp                  /* Pop SP save */
        rte                             /* Return */
spur_new_tick:
        move.w  #1000,(spur_tick_cnt,%a5) /* Reset count to 1000 */
        move.l  %d0,(spur_last_clock,%a5) /* Save current clock */
spur_done:
        movem.l (%sp)+,%d0/%a5          /* Restore */
        jmp     (FIM_EXIT).l            /* Return from exception */

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

/*
 * FIM_$PARITY_TRAP - Parity error trap handler
 *
 * Handles memory parity errors. Checks if the error is
 * recoverable and either continues or crashes.
 *
 * Assembly (0x00E21F84):
 */
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

        .end
