/*
 * fp/sau2/fp_context.s - 68881/68882 context switching (m68k / SAU2)
 *
 * Transcribed from the image.  These routines pass their arguments in
 * registers and fall through into one another, so they are hand-written
 * assembly, not compiled Pascal:
 *
 *   fp_$switch_owner      0x00E21B10   6 bytes, falls through
 *   fp_$switch_owner_d2   0x00E21B16  26 bytes, falls through
 *   fp_$restore_state     0x00E21B30  44 bytes   (D2 = asid, A1 = save base)
 *   fp_$save_state        0x00E21B5C  36 bytes   (D0 = asid, A1 = save base)
 *   FP_$GET_FP            0x00E21D48  40 bytes   (asid on the stack)
 *   fp_$check_owner       0x00E21D70  36 bytes
 *   FP_$PUT_FP            0x00E21D94  46 bytes   (asid on the stack)
 *
 * A1 is loaded once (from FP_$SAVEP) by fp_$switch_owner_d2 or
 * fp_$check_owner and is then relied on by the save/restore helper that
 * runs next, which is why FP_$GET_FP / FP_$PUT_FP cannot be expressed as
 * ordinary C.
 *
 * ---------------------------------------------------------------------
 * The FP save area (source-djly)
 * ---------------------------------------------------------------------
 * FP_$SAVEP (0x00E218D0) is NOT a "save pending" flag: it is a LONGWORD
 * POINTER to the per-address-space save-area table, allocated at boot by
 * PEB_$LOAD_WCS (`move.l A0,(0x00e218d0).l` at 0x00E3207C, from the
 * allocator at 0x00E43982) and cleared by OS_$SHUTDOWN (0x00E6D52E).  It
 * is zero in the image, and `tst.l` on it is the "no FPU configured"
 * test (FIM_$FP_ABORT 0x00E21B80, FIM_$FP_INIT 0x00E21BB0).
 *
 * Each address space owns FP_SAVE_AREA_SIZE = 0x14A bytes.  The LAST
 * longword of a slot, at (-0x4,A1,asid*0x14A), holds a pointer to the top
 * of that AS's saved frame; the frame itself is written DOWNWARD from
 * that cell by `fsave -(A0)` and the `fmovem ...,-(A0)` pair.  asid 0 is
 * "no address space" and is skipped by the `beq` after each `mulu.w`.
 *
 * 0x00E21928, previously used as an FP save-area base, is inside
 * FIM_$BUS_ERR (0x00E218E8, 484 bytes): it is the extension words of the
 * `cmpi.b #0xA0,(0x22,SP)` at 0x00E21926.  There is no fixed save-area
 * address in the image.
 */

        .section ".text.FP_$GET_FP","ax",@progbits
        .even

        .extern PROC1_$AS_ID            /* 0x00E2060A */
        .extern FP_$SAVEP               /* 0x00E218D0 (fim/sau2/fim.s) */
        .extern FP_$OWNER               /* 0x00E218D4 (fim/sau2/fim.s) */
        .extern FP_$EXCLUSION           /* 0x00E218D6 (fim/sau2/fim.s) */
        .extern ML_$EXCLUSION_START     /* 0x00E20DF8 */
        .extern ML_$EXCLUSION_STOP      /* 0x00E20E7E */

/* Hardware FPU-owner register: the low byte of the owning AS id is
 * mirrored here so the MMU/coprocessor interface can trap foreign use. */
        .set    FP_HW_OWNER, 0x00FFB402

/* Bytes per address space in the save-area table. */
        .set    FP_SAVE_AREA_SIZE, 0x14A

/* FPCR loaded when an AS has no saved state: round-to-nearest, extended
 * precision, all traps disabled (0x00E21B3E `fmove.l #0xf400,FPCR`). */
        .set    FP_DEFAULT_FPCR, 0xF400


/* ====================================================================
 * fp_$switch_owner - 0x00E21B10
 *
 * Make the CURRENT address space the FPU owner, saving the previous
 * owner's state and restoring the new owner's.  Falls through.
 * ==================================================================== */
        .global fp_$switch_owner
fp_$switch_owner:
        move.w  (PROC1_$AS_ID).l, %d2   /* 00e21b10  34 39 00 e2 06 0a */
        /* fall through */

/* ====================================================================
 * fp_$switch_owner_d2 - 0x00E21B16
 *
 * Same, with the desired AS id already in D2.  Reached from FIM_$FLINE
 * (`bsr` at 0x00E21AF0).  Falls through into fp_$restore_state.
 * ==================================================================== */
        .global fp_$switch_owner_d2
fp_$switch_owner_d2:
        move.w  (FP_$OWNER:w,%pc), %d0     /* 00e21b16  30 3a fd bc -> 0xE218D4 */
        movea.l (FP_$SAVEP:w,%pc), %a1     /* 00e21b1a  22 7a fd b4 -> 0xE218D0 */
        cmp.w   %d0, %d2                /* 00e21b1e  already the owner? */
        beq.s   .Lfp_done               /* 00e21b20  -> 0x00E21B5A */
        move.w  %d2, (FP_$OWNER).l      /* 00e21b22 */
        move.b  %d2, (FP_HW_OWNER).l    /* 00e21b28  low byte to the hardware */
        bsr.s   fp_$save_state          /* 00e21b2e  D0 = the PREVIOUS owner */
        /* fall through into fp_$restore_state with D2 = the new owner */

/* ====================================================================
 * fp_$restore_state - 0x00E21B30
 *
 * Input:  D2.w = asid, A1 = FP_$SAVEP
 * Restores the AS's saved frame, or initialises the FPU if it has none.
 * ==================================================================== */
        .global fp_$restore_state
fp_$restore_state:
        mulu.w  #FP_SAVE_AREA_SIZE, %d2 /* 00e21b30  byte offset of the slot */
        beq.s   .Lfp_done               /* 00e21b34  asid 0 == no address space */
        move.l  (-4,%a1,%d2.l), %d0     /* 00e21b36  slot's saved-frame pointer */
        movea.l %d0, %a0                /* 00e21b3a */
        bne.s   .Lrs_have_frame         /* 00e21b3c */

        /* No saved frame: leave the FPU idle with the default FPCR. */
        fmove.l #FP_DEFAULT_FPCR, %fpcr /* 00e21b3e */
        bra.s   .Lfp_done               /* 00e21b46 */

.Lrs_have_frame:
        /*
         * (0x1,A0) is the second byte of the frame's first word.  For a
         * 68881/68882 FSAVE frame that byte is the frame size; zero means
         * a NULL frame, i.e. only the FSAVE header was written and there
         * are no register contents in front of it.
         */
        tst.b   (1,%a0)                 /* 00e21b48 */
        beq.s   .Lrs_frestore           /* 00e21b4c */

        addq.w  #2, %a0                 /* 00e21b4e  skip the 0xFFFF marker */
        fmovem.l (%a0)+, %fpcr/%fpsr/%fpiar     /* 00e21b50 */
        fmovem.x (%a0)+, %fp0-%fp7              /* 00e21b54 */

.Lrs_frestore:
        frestore (%a0)+                 /* 00e21b58 */

.Lfp_done:
        rts                             /* 00e21b5a */

/* ====================================================================
 * fp_$save_state - 0x00E21B5C
 *
 * Input:  D0.w = asid, A1 = FP_$SAVEP
 * Writes the frame downward from the end of the AS's slot and stores the
 * resulting top-of-frame pointer in the slot's last longword.
 * ==================================================================== */
        .global fp_$save_state
fp_$save_state:
        mulu.w  #FP_SAVE_AREA_SIZE, %d0 /* 00e21b5c */
        beq.s   .Lss_done               /* 00e21b60  asid 0 == no address space */
        lea     (-4,%a1,%d0.l), %a0     /* 00e21b62  A0 = &slot->state */
        fsave   -(%a0)                  /* 00e21b66  frame grows downward */
        tst.b   (1,%a0)                 /* 00e21b68  NULL frame? */
        beq.s   .Lss_store              /* 00e21b6c  yes: nothing else to save */
        fmovem.x %fp0-%fp7, -(%a0)              /* 00e21b6e */
        fmovem.l %fpcr/%fpsr/%fpiar, -(%a0)     /* 00e21b72 */
        move.w  #-1, -(%a0)             /* 00e21b76  0xFFFF "registers present" */
.Lss_store:
        move.l  %a0, (-4,%a1,%d0.l)     /* 00e21b7a */
.Lss_done:
        rts                             /* 00e21b7e */

/* ====================================================================
 * FP_$GET_FP - 0x00E21D48
 *
 *   void FP_$GET_FP(uint16_t asid)      asid at (0x4,SP)
 *
 * Takes the FP exclusion lock, makes the CURRENT address space the owner
 * (saving whoever held the FPU), then restores the CALLER-SUPPLIED asid's
 * state.  Note that D2 is reloaded from (0x8,SP) after the push, so the
 * restored state is the argument's, not necessarily the new owner's.
 * ==================================================================== */
        .global FP_$GET_FP
FP_$GET_FP:
        pea     (FP_$EXCLUSION:w,%pc)      /* 00e21d48  48 7a fb 8c -> 0xE218D6 */
        jsr     (ML_$EXCLUSION_START).l /* 00e21d4c  0x00E20DF8 */
        addq.l  #4, %sp                 /* 00e21d52 */
        move.l  %d2, -(%sp)             /* 00e21d54 */
        bsr.s   fp_$check_owner         /* 00e21d56  sets A1 = FP_$SAVEP */
        move.w  (0x8,%sp), %d2          /* 00e21d58  asid (4 arg + 4 saved D2) */
        bsr.w   fp_$restore_state       /* 00e21d5c */
        move.l  (%sp)+, %d2             /* 00e21d60 */
        pea     (FP_$EXCLUSION:w,%pc)      /* 00e21d62 */
        jsr     (ML_$EXCLUSION_STOP).l  /* 00e21d66  0x00E20E7E */
        addq.l  #4, %sp                 /* 00e21d6c */
        rts                             /* 00e21d6e */

/* ====================================================================
 * fp_$check_owner - 0x00E21D70
 *
 * Like fp_$switch_owner but WITHOUT the fall-through into the restore:
 * it only takes ownership and saves the previous owner's state.
 * Always leaves A1 = FP_$SAVEP.
 * ==================================================================== */
        .global fp_$check_owner
fp_$check_owner:
        move.w  (PROC1_$AS_ID).l, %d2   /* 00e21d70 */
        move.w  (FP_$OWNER:w,%pc), %d0     /* 00e21d76  30 3a fb 5c -> 0xE218D4 */
        movea.l (FP_$SAVEP:w,%pc), %a1     /* 00e21d7a  22 7a fb 54 -> 0xE218D0 */
        cmp.w   %d0, %d2                /* 00e21d7e */
        beq.s   .Lco_done               /* 00e21d80 */
        move.w  %d2, (FP_$OWNER).l      /* 00e21d82 */
        move.b  %d2, (FP_HW_OWNER).l    /* 00e21d88 */
        bsr.w   fp_$save_state          /* 00e21d8e  D0 = the PREVIOUS owner */
.Lco_done:
        rts                             /* 00e21d92 */

/* ====================================================================
 * FP_$PUT_FP - 0x00E21D94
 *
 *   void FP_$PUT_FP(uint16_t asid)      asid at (0x4,SP)
 *
 * Takes the FP exclusion lock, makes the CURRENT address space the owner
 * (which restores its state through the fall-through in
 * fp_$switch_owner), then saves the CALLER-SUPPLIED asid's state.
 * ==================================================================== */
        .global FP_$PUT_FP
FP_$PUT_FP:
        pea     (FP_$EXCLUSION:w,%pc)      /* 00e21d94 */
        jsr     (ML_$EXCLUSION_START).l /* 00e21d98 */
        addq.l  #4, %sp                 /* 00e21d9e */
        movem.l %d2-%d3, -(%sp)         /* 00e21da0  48 e7 30 00 */
        bsr.w   fp_$switch_owner        /* 00e21da4  leaves A1 = FP_$SAVEP */
        move.w  (0xc,%sp), %d0          /* 00e21da8  asid (4 arg + 8 saved) */
        bsr.w   fp_$save_state          /* 00e21dac */
        movem.l (%sp)+, %d2-%d3         /* 00e21db0  4c df 00 0c */
        pea     (FP_$EXCLUSION:w,%pc)      /* 00e21db4 */
        jsr     (ML_$EXCLUSION_STOP).l  /* 00e21db8 */
        addq.l  #4, %sp                 /* 00e21dbe */
        rts                             /* 00e21dc0 */

        .end
