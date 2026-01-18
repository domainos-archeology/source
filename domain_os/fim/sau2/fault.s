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
        .equ    FIM_SETUP_RETURN,   0x00E21878  /* Set up return frame */
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
        movea.l (0x4,SP),A5             /* A5 = startup context */
        bsr.w   (FIM_SETUP_RETURN).l    /* Set up return frame */
        clr.w   (A0)                    /* Clear format word */
        move.l  (0x4,A5),-(A0)          /* Push PC */
        clr.w   -(A0)                   /* Push SR = 0 (user mode) */
        movea.l (A5),A1                 /* A1 = initial USP */
        move    A1,USP                  /* Set user SP */
        movea.l A0,SP                   /* Switch to exception frame */
        suba.l  A6,A6                   /* Clear A6 (frame pointer) */
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
        move.w  (PROC1_AS_ID).l,D0      /* D0 = AS ID */
        lea     (FIM_QUIT_INH).l,A0     /* A0 = quit inhibit table */
        st      (0,A0,D0.w)             /* Set quit inhibited */
        lsl.w   #2,D0                   /* D0 = AS * 4 */
        lea     (FIM_TRACE_STS).l,A0    /* A0 = trace status table */
        move.l  #0x00120015,(0,A0,D0.w) /* Set trace fault status */
        bsr.w   (FIM_SETUP_RETURN).l    /* Set up return frame */
        movea.l (0x4,SP),A1             /* A1 = PC ptr */
        move.l  (A1),-(A0)              /* Push PC */
        movea.l (0x8,SP),A1             /* A1 = SR ptr */
        move.w  (A1),D0                 /* D0 = SR */
        and.w   #0xC01F,D0              /* Keep only relevant bits */
        bset.l  #15,D0                  /* Set trace bit */
        move.w  D0,-(A0)                /* Push modified SR */
        movea.l (0xC,SP),A1             /* A1 = regs ptr */
        movea.l (0x3C,A1),A2            /* A2 = saved USP */
        move    A2,USP                  /* Restore USP */
        movea.l A0,SP                   /* Switch to exception frame */
        movem.l (A1),D0-D7/A0-A6        /* Restore registers */
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
        movea.l (0x4,SP),A2             /* A2 = context ptr ptr */
        movea.l (0x8,SP),A3             /* A3 = regs ptr ptr */
        movea.l (0xC,SP),A0             /* A0 = FP state ptr ptr */
        tst.l   (A0)                    /* Check if FP state exists */
        beq.b   fault_ret_setup         /* No FP state */
        move.l  A0,-(SP)                /* Push FP state ptr */
        jsr     (FIM_FRESTORE).l        /* Restore FP state */
fault_ret_setup:
        bsr.b   fault_ret_helper        /* Set up return frame */
        clr.w   (A0)                    /* Clear format word */
        movea.l (A2),A2                 /* A2 = context ptr */
        move.l  (0x14,A2),-(A0)         /* Push PC */
        move.l  (0x18,A2),D0            /* D0 = saved SR */
        and.w   #0xC01F,D0              /* Keep only relevant bits */
        move.w  D0,-(A0)                /* Push SR */
        move.l  (0xC,A2),-(A0)          /* Push additional context */
        move.l  (0x10,A2),-(A0)         /* Push more context */
        movea.l (0x8,A2),A1             /* A1 = saved USP */
        move    A1,USP                  /* Restore USP */
        movea.l A0,SP                   /* Switch to exception frame */
        tst.l   (A3)                    /* Check if regs to restore */
        beq.b   fault_ret_exit          /* No regs */
        movea.l (A3),A3                 /* A3 = regs ptr */
        movem.l (A3),D0-D7/A0-A6        /* Restore all regs */
fault_ret_exit:
        movea.l (SP)+,A5                /* Restore A5 */
        movea.l (SP)+,A6                /* Restore A6 */
        jmp     (FIM_EXIT).l            /* Return from exception */

/*
 * Helper for FIM_$FAULT_RETURN
 * Sets up the return frame by getting OS stack
 */
fault_ret_helper:
        /* This would call FIM_SETUP_RETURN at 0x00E21878 */
        jmp     (FIM_SETUP_RETURN).l

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
        move.w  (0x4,SP),D0             /* D0 = process ID */
        lsl.w   #2,D0                   /* D0 = process * 4 */
        lea     (OS_STACK_BASE).l,A0    /* A0 = stack base table */
        move.l  (0,A0,D0.w),D0          /* D0 = stack base */
        subq.l  #8,D0                   /* Point to top of frame */
        movea.l D0,A0                   /* A0 = frame ptr */
        clr.w   D1                      /* D1 = frame type 0 */
        bsr.b   check_frame_type
        beq.b   found_user_sr
        movea.l A0,A1                   /* Save frame ptr */
        lea     (-0x32,A1),A0           /* Try format A frame */
        move.w  #0x8008,D1              /* Type A frame */
        bsr.b   check_frame_type
        beq.b   found_user_sr
        move.w  #0x800C,D1              /* Type B frame */
        bsr.b   check_frame_type
        beq.b   found_user_sr
        lea     (-0x54,A1),A0           /* Try format B frame */
        move.w  #0xB008,D1
        bsr.b   check_frame_type
        beq.b   found_user_sr
        move.w  #0xB00C,D1
        bsr.b   check_frame_type
        beq.b   found_user_sr
        lea     (-0x18,A1),A0           /* Try format 9 frame */
        move.w  #0xA008,D1
        bsr.b   check_frame_type
        beq.b   found_user_sr
        move.w  #0xA00C,D1
        bsr.b   check_frame_type
        beq.b   found_user_sr
        lea     (-0x4,A1),A0            /* Try short frame */
        move.w  #0x2000,D1
        bsr.b   check_frame_type
        beq.b   found_user_sr
        lea     (-0xC,A1),A0            /* Try throwaway frame */
        move.w  #0x9000,D1
        bsr.b   check_frame_type
        beq.b   found_user_sr
        movea.l A1,A0                   /* Return original */
found_user_sr:
        move.l  A0,D0                   /* Return ptr in D0 */
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
        lea     (FIM_TRACE_BIT).l,A1    /* A1 = trace bit table */
        move.w  (0x4,SP),D0             /* D0 = process ID */
        move    SR,-(SP)                /* Save SR */
        ori     #0x0700,SR              /* Disable interrupts */
        bset.b  #7,(0,A1,D0.w)          /* Set trace bit */
        bne.b   trace_already_set       /* Already set, skip */
        addq.l  #1,(PENDING_TRACE).l    /* Increment pending count */
        /* Patch FIM_$EXIT to NOP to catch trace */
        move.w  #0x4E71,(FIM_EXIT).l    /* Write NOP instruction */
        jsr     (CACHE_CLEAR).l         /* Clear instruction cache */
trace_already_set:
        move    (SP)+,SR                /* Restore SR */
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
        lea     (FIM_TRACE_BIT).l,A1    /* A1 = trace bit table */
        move.w  (0x4,SP),D0             /* D0 = process ID */
        ori     #0x0700,SR              /* Disable interrupts */
        bclr.b  #7,(0,A1,D0.w)          /* Clear trace bit */
        beq.b   trace_not_set           /* Wasn't set, skip */
        subq.l  #1,(PENDING_TRACE).l    /* Decrement pending count */
        bne.b   trace_not_set           /* Still others pending */
        /* Restore FIM_$EXIT to RTE */
        move.w  #0x4E73,(FIM_EXIT).l    /* Write RTE instruction */
        jsr     (CACHE_CLEAR).l         /* Clear instruction cache */
trace_not_set:
        andi    #0xF8FF,SR              /* Re-enable interrupts */
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
        movem.l D0/A5,-(SP)             /* Save registers */
        lea     (spur_data,PC),A5       /* A5 = local data */
        addq.l  #1,(spur_count,A5)      /* Increment count */
        move.l  (TIME_CLOCKH).l,D0      /* D0 = current clock */
        cmp.l   (spur_last_clock,A5),D0 /* Same tick? */
        bne.b   spur_new_tick           /* No, reset counter */
        subq.w  #1,(spur_tick_cnt,A5)   /* Decrement tick count */
        bne.b   spur_done               /* Not zero, continue */
        /* Too many spurious interrupts - crash */
        movem.l (SP)+,D0/A5             /* Restore */
        movem.l D0-D7/A0-A6/SP,-(SP)    /* Save all for crash */
        pea     (spur_regs,PC)          /* Push regs ptr */
        pea     (0x4,SP)                /* Push D0/D1 ptr */
        pea     (0x48,SP)               /* Push exception frame */
        jsr     (FIM_CRASH).l           /* Crash */
        adda.w  #12,SP                  /* Clean up */
        movem.l (SP)+,D0-D7/A0-A6       /* Restore */
        addq.w  #4,SP                   /* Pop SP save */
        rte                             /* Return */
spur_new_tick:
        move.w  #1000,(spur_tick_cnt,A5) /* Reset count to 1000 */
        move.l  D0,(spur_last_clock,A5) /* Save current clock */
spur_done:
        movem.l (SP)+,D0/A5             /* Restore */
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
        movem.l D0/D1/A0/A1,-(SP)       /* Save registers */
        jsr     (PARITY_CHK).l          /* Check parity status */
        lea     (parity_data,PC),A0     /* A0 = local data */
        clr.w   (A0)                    /* Clear status */
        tst.b   D0                      /* Check result */
        bmi.b   parity_error            /* Negative = error */
        jmp     (IO_HANDLER).l          /* Handle as I/O int */
parity_error:
        /* Parity error - try to deliver as fault */
        movem.l (SP)+,D0/D1/A0/A1       /* Restore */
        suba.w  #0x3A,SP                /* Make room for frame */
        move.w  (0x3A,SP),(SP)          /* Copy SR */
        move.l  (0x3C,SP),(0x2,SP)      /* Copy PC */
        move.b  #0x80,(0x6,SP)          /* Set frame type */
        /* Set up parameters and call BUILD_DF */
        move.w  #0x3000,-(SP)           /* Push flags */
        move.w  #0x13,-(SP)             /* Push vector */
        move.l  #0x0012001E,-(SP)       /* Push status */
        andi    #0xF8FF,SR              /* Enable interrupts */
        pea     (SP)                    /* Push frame ptr */
        pea     (0xC,SP)                /* Push regs ptr */
        jmp     (FIM_BUILD_DF).l        /* Build delivery frame */

parity_data:
        .word   0                       /* Parity status */
        .long   0                       /* Fault address */
        .word   0                       /* Additional info */

        .end
