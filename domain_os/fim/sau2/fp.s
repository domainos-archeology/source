/*
 * fim/sau2/fp.s - FIM Floating Point Routines (m68k/SAU2)
 *
 * These routines manage 68881/68882 FPU state for the kernel.
 * They handle saving and restoring FP state during context switches
 * and exception handling.
 *
 * Original addresses:
 *   FIM_$FP_ABORT:     0x00E21B80 (48 bytes)
 *   FIM_$FP_INIT:      0x00E21BB0 (84 bytes)
 *   FIM_$FSAVE:        0x00E21C34 (160 bytes)
 *   FIM_$FRESTORE:     0x00E21CD4 (116 bytes)
 *   FIM_$FP_GET_STATE: 0x00E21DC2 (196 bytes)
 *   FIM_$FP_PUT_STATE: 0x00E21E86 (152 bytes)
 */

        .text
        .even

/*
 * External references
 */
        .equ    M68881_EXISTS,      0x00E8180C  /* FPU present flag */
        .equ    FP_SAVEP,           0x00E218D0  /* FP save pending flag */
        .equ    ML_EXCLUSION,       0x00E218D6  /* FP exclusion lock */
        .equ    ML_EXCLUSION_START, 0x00E20DF8  /* Start exclusion */
        .equ    ML_EXCLUSION_STOP,  0x00E20E7E  /* Stop exclusion */
        .equ    FP_OWNER_SWITCH,    0x00E21B10  /* FP owner switch */
        .equ    FP_COPY_STATE,      0x00E21C04  /* Copy FP state */
        .equ    FIM_COMMON_FAULT,   0x00E213A4  /* Common fault */

/*
 * FIM_$FP_ABORT - Abort floating point operation
 *
 * Called when an FP operation cannot complete (e.g., during
 * exception handling). Clears the FPU state.
 *
 * Assembly (0x00E21B80):
 */
        .global FIM_$FP_ABORT
FIM_$FP_ABORT:
        movem.l %d0/%d1/%d2/%d3/%a0/%a1/%a2/%a3,-(%sp)
        pea     (ML_EXCLUSION).l        /* Get exclusion */
        jsr     (ML_EXCLUSION_START).l
        addq.l  #4,%sp
        tst.l   (FP_SAVEP).l            /* Check if save pending */
        beq.b   fp_abort_clear
        bsr.w   (FP_OWNER_SWITCH).l     /* Switch FP owner */
fp_abort_clear:
        clr.l   -(%sp)                  /* Push NULL state */
        frestore (%sp)+                 /* Clear FPU state */
        pea     (ML_EXCLUSION).l        /* Release exclusion */
        jsr     (ML_EXCLUSION_STOP).l
        addq.l  #4,%sp
        movem.l (%sp)+,%d0/%d1/%d2/%d3/%a0/%a1/%a2/%a3
        rts

/*
 * FIM_$FP_INIT - Initialize floating point state
 *
 * Initializes the 68881/68882 FPU for a new process.
 * Sets up default control register values.
 *
 * Assembly (0x00E21BB0):
 */
        .global FIM_$FP_INIT
FIM_$FP_INIT:
        movem.l %d0/%d1/%d2/%d3/%a0/%a1/%a2/%a3,-(%sp)
        tst.w   (M68881_EXISTS).l       /* Check if FPU exists */
        beq.b   fp_init_done            /* No FPU, nothing to do */
        pea     (ML_EXCLUSION).l        /* Get exclusion */
        jsr     (ML_EXCLUSION_START).l
        addq.l  #4,%sp
        tst.l   (FP_SAVEP).l            /* Check save pending */
        beq.b   fp_init_clear
        bsr.w   (FP_OWNER_SWITCH).l     /* Switch FP owner */
fp_init_clear:
        clr.l   -(%sp)                  /* Clear state word */
        frestore (%sp)+                 /* Reset FPU to idle */
        fmove.l #0,%fpcr                /* Clear control register */
        fmove.l #0,%fpsr                /* Clear status register */
        fmove.l #0,%fpiar               /* Clear instruction addr */
        pea     (ML_EXCLUSION).l        /* Release exclusion */
        jsr     (ML_EXCLUSION_STOP).l
        addq.l  #4,%sp
fp_init_done:
        movem.l (%sp)+,%d0/%d1/%d2/%d3/%a0/%a1/%a2/%a3
        rts

/*
 * FIM_$FSAVE - Save floating point state
 *
 * Saves the 68881/68882 internal state using the FSAVE instruction.
 * Used during exception handling to preserve FP context.
 *
 * Parameters:
 *   0x24(SP) - Pointer to status word (set to 0 if no state, 1 if saved)
 *   0x28(SP) - Pointer to stack pointer (updated with state location)
 *   0x2C(SP) - Save type (1 = save and clear)
 *   0x2E(SP) - Unused
 *
 * Assembly (0x00E21C34):
 */
        .global FIM_$FSAVE
FIM_$FSAVE:
        movem.l %d0/%d1/%d2/%d3/%a0/%a1/%a2/%a3,-(%sp)
        pea     (ML_EXCLUSION).l        /* Get exclusion */
        jsr     (ML_EXCLUSION_START).l
        addq.l  #4,%sp
        tst.l   (FP_SAVEP).l            /* Check if save pending */
        beq.b   fsave_check_type
        bsr.w   (FP_OWNER_SWITCH).l     /* Switch FP owner */
fsave_check_type:
        movea.l (0x24,%sp),%a3          /* A3 = status ptr */
        clr.w   (%a3)                   /* Default: no state */
        movea.l %sp,%a3                 /* Save SP for reference */
        cmpi.w  #1,(0x2C,%a3)           /* Check save type */
        bne.b   fsave_normal
        /* Type 1: Save state, clear FPU, then restore control regs */
        fsave   -(%sp)                  /* Save internal state */
        tst.w   (M68881_EXISTS).l       /* Check FPU exists */
        beq.w   fsave_set_flag
        bset.b  #3,(0x38,%sp)           /* Mark state as modified */
        fmovem.l %fpcr/%fpsr/%fpiar,-(%sp) /* Save control registers */
        bclr.b  #0,(0x4,%sp)            /* Clear exception flag */
        clr.l   -(%sp)                  /* Push null state */
        frestore (%sp)+                 /* Reset FPU */
        fmovem.l (%sp)+,%fpcr/%fpsr/%fpiar /* Restore control regs */
fsave_normal:
        tst.w   (M68881_EXISTS).l       /* Check FPU exists */
        beq.b   fsave_no_state
        fsave   -(%sp)                  /* Save internal state */
        cmpi.w  #0x3FD4,(%sp)           /* Check for busy state */
        beq.b   fsave_set_flag          /* Busy = has state */
fsave_no_state:
        movea.l %a3,%sp                 /* Restore SP (no state) */
        clr.w   -(%sp)                  /* Push 0 (no state flag) */
        bra.b   fsave_release
fsave_set_flag:
        move.w  #1,-(%sp)               /* Push 1 (state saved) */
fsave_release:
        pea     (ML_EXCLUSION).l        /* Release exclusion */
        jsr     (ML_EXCLUSION_STOP).l
        addq.l  #4,%sp
        tst.w   (%sp)                   /* Check if state saved */
        bne.b   fsave_copy_state
        addq.l  #2,%sp                  /* Pop flag */
        bra.b   fsave_done
fsave_copy_state:
        /* Copy state to caller's buffer */
        movea.l %sp,%a0                 /* A0 = source (stack) */
        movea.l (0x28,%a3),%a1          /* A1 = dest ptr ptr */
        movea.l (%a1),%a1               /* A1 = dest ptr */
        jsr     (FP_COPY_STATE).l       /* Copy the state */
        movea.l %a3,%sp                 /* Restore SP */
        movea.l (0x28,%sp),%a3          /* Get dest ptr ptr */
        move.l  %a1,(%a3)               /* Update dest ptr */
        movea.l (0x24,%sp),%a3          /* Get status ptr */
        move.w  #1,(%a3)                /* Set status = saved */
fsave_done:
        movem.l (%sp)+,%d0/%d1/%d2/%d3/%a0/%a1/%a2/%a3
        rts

/*
 * FIM_$FRESTORE - Restore floating point state
 *
 * Restores 68881/68882 state from a saved buffer.
 * Validates checksum before restoring.
 *
 * Parameters:
 *   0x24(SP) - Pointer to state buffer pointer
 *
 * Assembly (0x00E21CD4):
 */
        .global FIM_$FRESTORE
FIM_$FRESTORE:
        movem.l %d0/%d1/%d2/%d3/%a0/%a1/%a2/%a3,-(%sp)
        movea.l %sp,%a3                 /* Save SP for reference */
        movea.l (0x24,%sp),%a0          /* A0 = buffer ptr ptr */
        movea.l (%a0),%a0               /* A0 = buffer ptr */
        move.l  (%a0)+,%d1              /* D1 = size/format word */
        move.l  (%a0)+,%d0              /* D0 = checksum */
        adda.l  %d1,%a0                 /* A0 = end of data */
        lsr.l   #1,%d1                  /* D1 = word count */
        subq.l  #1,%d1                  /* D1 = loop count */
        clr.l   %d3                     /* D3 = computed checksum */
        clr.l   %d2                     /* D2 = temp */
frestore_checksum:
        move.w  -(%a0),%d2              /* Get word */
        add.l   %d2,%d3                 /* Add to checksum */
        move.w  %d2,-(%sp)              /* Push to stack */
        dbf     %d1,frestore_checksum   /* Loop */
        cmp.l   %d3,%d0                 /* Verify checksum */
        beq.b   frestore_valid
        /* Checksum mismatch - fault */
        movea.l %a3,%sp                 /* Restore SP */
        movem.l (%sp)+,%d0/%d1/%d2/%d3/%a0/%a1/%a2/%a3
        addq.w  #8,%sp                  /* Clean up */
        move.w  (0x2000).w,-(%sp)       /* Push status */
        move.w  #0x13,-(%sp)            /* Push code */
        move.l  #0x12002D,-(%sp)        /* Push error status */
        pea     (%sp)                   /* Push ptr to status */
        pea     (0xC,%sp)               /* Push ptr */
        jmp     (FIM_COMMON_FAULT).l    /* Signal fault */
frestore_valid:
        cmpi.w  #1,(%sp)+               /* Check for saved state */
        bne.b   frestore_done           /* No state, done */
        pea     (ML_EXCLUSION).l        /* Get exclusion */
        jsr     (ML_EXCLUSION_START).l
        addq.l  #4,%sp
        bsr.w   (FP_OWNER_SWITCH).l     /* Switch FP owner */
        frestore (%sp)+                 /* Restore FPU state */
        pea     (ML_EXCLUSION).l        /* Release exclusion */
        jsr     (ML_EXCLUSION_STOP).l
        addq.l  #4,%sp
frestore_done:
        movem.l (%sp)+,%d0/%d1/%d2/%d3/%a0/%a1/%a2/%a3
        rts

/*
 * FIM_$FP_GET_STATE - Get complete FP state
 *
 * Gets the complete FPU state including all registers.
 * Used for debugging and process state inspection.
 *
 * Parameters:
 *   4(SP) - Pointer to state buffer
 *   8(SP) - Pointer to status return
 *
 * Assembly (0x00E21DC2):
 */
        .global FIM_$FP_GET_STATE
FIM_$FP_GET_STATE:
        movem.l %d0/%d1/%d2/%d3/%a0/%a1/%a2/%a3,-(%sp)
        tst.w   (M68881_EXISTS).l       /* Check if FPU exists */
        beq.b   fp_get_no_fpu
        pea     (ML_EXCLUSION).l        /* Get exclusion */
        jsr     (ML_EXCLUSION_START).l
        addq.l  #4,%sp
        tst.l   (FP_SAVEP).l            /* Check save pending */
        beq.b   fp_get_save
        bsr.w   (FP_OWNER_SWITCH).l     /* Switch FP owner */
fp_get_save:
        movea.l (0x24,%sp),%a0          /* A0 = state buffer */
        fsave   -(%sp)                  /* Save internal state */
        /* Copy FP registers and internal state to buffer */
        fmovem.l %fpcr/%fpsr/%fpiar,(%a0) /* Save control regs */
        lea     (0xC,%a0),%a0           /* Skip control regs */
        fmovem.x %fp0-%fp7,(%a0)        /* Save FP registers */
        /* Restore FPU state */
        frestore (%sp)+                 /* Restore internal state */
        pea     (ML_EXCLUSION).l        /* Release exclusion */
        jsr     (ML_EXCLUSION_STOP).l
        addq.l  #4,%sp
        movea.l (0x28,%sp),%a0          /* A0 = status ptr */
        clr.l   (%a0)                   /* Status = OK */
        bra.b   fp_get_done
fp_get_no_fpu:
        movea.l (0x28,%sp),%a0          /* A0 = status ptr */
        move.l  #0x00120040,(%a0)       /* Status = no FPU */
fp_get_done:
        movem.l (%sp)+,%d0/%d1/%d2/%d3/%a0/%a1/%a2/%a3
        rts

/*
 * FIM_$FP_PUT_STATE - Set complete FP state
 *
 * Sets the complete FPU state including all registers.
 * Used for debugging and process state modification.
 *
 * Parameters:
 *   4(SP) - Pointer to state buffer
 *   8(SP) - Pointer to status return
 *
 * Assembly (0x00E21E86):
 */
        .global FIM_$FP_PUT_STATE
FIM_$FP_PUT_STATE:
        movem.l %d0/%d1/%d2/%d3/%a0/%a1/%a2/%a3,-(%sp)
        tst.w   (M68881_EXISTS).l       /* Check if FPU exists */
        beq.b   fp_put_no_fpu
        pea     (ML_EXCLUSION).l        /* Get exclusion */
        jsr     (ML_EXCLUSION_START).l
        addq.l  #4,%sp
        tst.l   (FP_SAVEP).l            /* Check save pending */
        beq.b   fp_put_restore
        bsr.w   (FP_OWNER_SWITCH).l     /* Switch FP owner */
fp_put_restore:
        /* First reset the FPU */
        clr.l   -(%sp)                  /* Push null state */
        frestore (%sp)+                 /* Reset FPU */
        movea.l (0x24,%sp),%a0          /* A0 = state buffer */
        fmovem.l (%a0),%fpcr/%fpsr/%fpiar /* Restore control regs */
        lea     (0xC,%a0),%a0           /* Skip control regs */
        fmovem.x (%a0),%fp0-%fp7        /* Restore FP registers */
        pea     (ML_EXCLUSION).l        /* Release exclusion */
        jsr     (ML_EXCLUSION_STOP).l
        addq.l  #4,%sp
        movea.l (0x28,%sp),%a0          /* A0 = status ptr */
        clr.l   (%a0)                   /* Status = OK */
        bra.b   fp_put_done
fp_put_no_fpu:
        movea.l (0x28,%sp),%a0          /* A0 = status ptr */
        move.l  #0x00120040,(%a0)       /* Status = no FPU */
fp_put_done:
        movem.l (%sp)+,%d0/%d1/%d2/%d3/%a0/%a1/%a2/%a3
        rts

        .end
