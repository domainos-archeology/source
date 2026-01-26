/*
 * fp/sau2/fp_context.s - FP Context Switch Routines (m68k/SAU2)
 *
 * Low-level 68881/68882 FPU context switching routines.
 * These routines save and restore FPU state between address spaces.
 *
 * Original addresses:
 *   fp_$switch_owner:  0x00E21B10 (6 bytes entry, falls through)
 *   fp_$check_owner:   0x00E21D70 (36 bytes)
 *   fp_$save_state:    0x00E21B5C (36 bytes)
 *   fp_$restore_state: 0x00E21B30 (44 bytes)
 */

        .text
        .even

/*
 * External references
 */
        .equ    PROC1_AS_ID,        0x00E2060A  /* Current AS ID */
        .equ    FP_OWNER,           0x00E218D4  /* Current FP owner */
        .equ    FP_HW_OWNER,        0x00FFB402  /* Hardware FP owner register */
        .equ    FP_SAVE_AREA_BASE,  0x00E21928  /* FP save area base */
        .equ    FP_SAVE_AREA_SIZE,  0x14A       /* 330 bytes per AS */
        .equ    FP_DEFAULT_FPCR,    0x0000F400  /* Default FPCR value */

/*
 * fp_$switch_owner - Switch FP owner and restore state
 *
 * Entry point for FP owner switching. Sets PROC1_$AS_ID as the
 * new FP owner. If different from current owner, saves current
 * owner's state and restores new owner's state.
 *
 * This is the main context switch routine called by FP_$PUT_FP.
 *
 * Uses:
 *   D2 - AS ID for operations
 *   A1 - Base pointer for save area
 *
 * Two entry points:
 *   fp_$switch_owner:    Loads D2 from PROC1_$AS_ID (0x00E21B10)
 *   fp_$switch_owner_d2: Uses D2 as-is (0x00E21B16) - caller sets D2
 *
 * Assembly (0x00E21B10):
 *   move.w  (PROC1_AS_ID).l,D2      ; D2 = current AS ID
 *   ; Falls through to fp_$switch_owner_d2
 */
        .global fp_$switch_owner
        .global fp_$switch_owner_d2
fp_$switch_owner:
        move.w  (PROC1_AS_ID).l,%d2     /* D2 = PROC1_$AS_ID */
        /* Fall through to fp_$switch_owner_d2 */
fp_$switch_owner_d2:
        /* Alternate entry point: caller has already set D2 to AS ID */
        move.w  (FP_OWNER).l,%d0        /* D0 = current FP owner */
        lea     (FP_SAVE_AREA_BASE).l,%a1 /* A1 = save area base */
        cmp.w   %d0,%d2                 /* Same owner? */
        beq.b   switch_done             /* Yes, nothing to do */
        /* New owner - save current and restore new */
        move.w  %d2,(FP_OWNER).l        /* Set new owner */
        move.b  %d2,(FP_HW_OWNER).l     /* Set hardware owner */
        bsr.w   fp_$save_state          /* Save current owner's state */
        /* Now restore new owner's state */
        mulu.w  #FP_SAVE_AREA_SIZE,%d2  /* D2 = offset into save area */
        beq.b   switch_done             /* AS 0 has no FP state */
        move.l  (-4,%a1,%d2:l),%d0      /* D0 = state pointer */
        movea.l %d0,%a0                 /* A0 = state pointer */
        bne.b   restore_fp              /* Has state, restore it */
        fmove.l #FP_DEFAULT_FPCR,%fpcr  /* No state, set default FPCR */
        bra.b   switch_done
restore_fp:
        tst.b   (1,%a0)                 /* Check flags byte */
        beq.b   restore_internal        /* Only internal state */
        addq.w  #2,%a0                  /* Skip flags word */
        fmovem.l (%a0)+,%fpcr/%fpsr/%fpiar /* Restore control regs */
        fmovem.x (%a0)+,%fp0-%fp7       /* Restore FP registers */
restore_internal:
        frestore (%a0)+                 /* Restore internal state */
switch_done:
        rts

/*
 * fp_$check_owner - Check and set FP owner (without restore)
 *
 * Checks if PROC1_$AS_ID is already the FP owner.
 * If not, sets it as owner and saves the previous owner's state.
 * Does NOT restore the new owner's state.
 *
 * Used by FP_$GET_FP before restoring state separately.
 *
 * Assembly (0x00E21D70):
 *   move.w  (PROC1_AS_ID).l,D2      ; D2 = current AS ID
 *   move.w  FP_$OWNER,D0            ; D0 = current owner
 *   movea.l FP_SAVE_AREA_BASE,A1    ; A1 = save area base
 *   cmp.w   D0,D2                   ; Same owner?
 *   beq.b   done                    ; Yes, done
 *   move.w  D2,(FP_$OWNER).l        ; Set new owner
 *   move.b  D2,(FP_HW_OWNER).l      ; Set hardware owner
 *   bsr.w   fp_$save_state          ; Save current state
 * done:
 *   rts
 */
        .global fp_$check_owner
fp_$check_owner:
        move.w  (PROC1_AS_ID).l,%d2     /* D2 = PROC1_$AS_ID */
        move.w  (FP_OWNER).l,%d0        /* D0 = current FP owner */
        lea     (FP_SAVE_AREA_BASE).l,%a1 /* A1 = save area base */
        cmp.w   %d0,%d2                 /* Same owner? */
        beq.b   check_done              /* Yes, nothing to do */
        move.w  %d2,(FP_OWNER).l        /* Set new owner */
        move.b  %d2,(FP_HW_OWNER).l     /* Set hardware owner */
        bsr.w   fp_$save_state          /* Save current owner's state */
check_done:
        rts

/*
 * fp_$save_state - Save FP state for an address space
 *
 * Saves the current FPU state to the save area for the AS
 * specified in D0.w.
 *
 * Input:
 *   D0.w - Address space ID
 *   A1   - Base pointer for save area
 *
 * The state is saved in reverse order to make restore easier:
 *   1. FSAVE to save internal state
 *   2. If state byte != 0, save FP0-FP7 with FMOVEM.X
 *   3. Save FPCR/FPSR/FPIAR with FMOVEM.L
 *   4. Save flags word (0xFFFF if full state)
 *   5. Store pointer to saved state
 *
 * Assembly (0x00E21B5C):
 *   mulu.w  #0x14a,D0               ; D0 = AS * save area size
 *   beq.b   done                    ; AS 0 has no FP state
 *   lea     (-0x4,A1,D0.l),A0       ; A0 = &save_area[AS].state_ptr
 *   fsave   -(A0)                   ; Save internal state
 *   tst.b   (0x1,A0)                ; Check state byte
 *   beq.b   save_ptr                ; If 0, internal only
 *   fmovem.x FP7-FP0,-(A0)          ; Save FP registers
 *   fmovem.l FPCR/FPSR/FPIAR,-(A0)  ; Save control registers
 *   move.w  #-1,-(A0)               ; Save flags (0xFFFF)
 * save_ptr:
 *   move.l  A0,(-0x4,A1,D0.l)       ; Store state pointer
 * done:
 *   rts
 */
        .global fp_$save_state
fp_$save_state:
        mulu.w  #FP_SAVE_AREA_SIZE,%d0  /* D0 = offset into save area */
        beq.b   save_done               /* AS 0 has no FP state */
        lea     (-4,%a1,%d0:l),%a0      /* A0 = &save_area[AS].state_ptr */
        fsave   -(%a0)                  /* Save internal state */
        tst.b   (1,%a0)                 /* Check state byte (non-null frame?) */
        beq.b   save_ptr_only           /* If 0, only internal state */
        fmovem.x %fp7/%fp6/%fp5/%fp4/%fp3/%fp2/%fp1/%fp0,-(%a0) /* Save FP regs */
        fmovem.l %fpcr/%fpsr/%fpiar,-(%a0) /* Save control registers */
        move.w  #-1,-(%a0)              /* Save flags word (0xFFFF = full) */
save_ptr_only:
        move.l  %a0,(-4,%a1,%d0:l)      /* Store pointer to saved state */
save_done:
        rts

/*
 * fp_$restore_state - Restore FP state for an address space
 *
 * Restores the FPU state from the save area for the AS
 * specified in D2.w.
 *
 * Input:
 *   D2.w - Address space ID
 *   A1   - Base pointer for save area
 *
 * The state is restored in the order it was saved:
 *   1. Check if state exists (pointer != NULL)
 *   2. If flags == 0xFFFF, restore FPCR/FPSR/FPIAR and FP0-FP7
 *   3. FRESTORE to restore internal state
 *
 * Assembly (0x00E21B30):
 *   mulu.w  #0x14a,D2               ; D2 = AS * save area size
 *   beq.b   done                    ; AS 0 has no FP state
 *   move.l  (-0x4,A1,D2.l),D0       ; D0 = state pointer
 *   movea.l D0,A0                   ; A0 = state pointer
 *   bne.b   has_state               ; Non-NULL, restore it
 *   fmove.l #0xf400,FPCR            ; NULL = set default FPCR
 *   bra.b   done
 * has_state:
 *   tst.b   (0x1,A0)                ; Check flags byte
 *   beq.b   internal_only           ; If 0, internal only
 *   addq.w  #2,A0                   ; Skip flags word
 *   fmovem.l (A0)+,FPCR/FPSR/FPIAR  ; Restore control registers
 *   fmovem.x (A0)+,FP0-FP7          ; Restore FP registers
 * internal_only:
 *   frestore (A0)+                  ; Restore internal state
 * done:
 *   rts
 */
        .global fp_$restore_state
fp_$restore_state:
        mulu.w  #FP_SAVE_AREA_SIZE,%d2  /* D2 = offset into save area */
        beq.b   restore_done            /* AS 0 has no FP state */
        move.l  (-4,%a1,%d2:l),%d0      /* D0 = state pointer */
        movea.l %d0,%a0                 /* A0 = state pointer */
        bne.b   has_saved_state         /* Non-NULL, restore it */
        fmove.l #FP_DEFAULT_FPCR,%fpcr  /* NULL = set default FPCR */
        bra.b   restore_done
has_saved_state:
        tst.b   (1,%a0)                 /* Check state byte */
        beq.b   restore_internal_only   /* If 0, internal only */
        addq.w  #2,%a0                  /* Skip flags word */
        fmovem.l (%a0)+,%fpcr/%fpsr/%fpiar /* Restore control registers */
        fmovem.x (%a0)+,%fp0-%fp7       /* Restore FP registers */
restore_internal_only:
        frestore (%a0)+                 /* Restore internal state */
restore_done:
        rts

        .end
