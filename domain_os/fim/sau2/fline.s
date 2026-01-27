/*
 * fim/sau2/fline.s - FIM F-Line Exception Handler (m68k/SAU2)
 *
 * Handles F-Line (coprocessor) traps for lazy FPU context switching.
 * When a process executes an FPU instruction and doesn't currently
 * own the FPU, this handler saves the previous owner's FP state,
 * restores the new owner's state, and retries the instruction.
 * If no FPU is present or the process already owns the FPU,
 * forwards to FIM_$UII (unimplemented instruction handler).
 *
 * This is the handler for exception vector 0x2C (F-Line emulator).
 *
 * Original address: 0x00E21ACC (68 bytes)
 *
 * Original encoding notes:
 *   - FP_$SAVEP, FP_$OWNER, and fp_exclusion were accessed PC-relative
 *     in the ROM image. Here they are defined as local data to preserve
 *     that access pattern.
 *   - fp_$switch_owner+6 was called via bsr.b (short PC-relative) in ROM.
 *     Here we use jsr to the external symbol fp_$switch_owner_d2.
 *   - FIM_$EXIT was reached via jmp (PC-relative) in ROM. Here we use
 *     jmp to the external symbol.
 */

        .text
        .even

/*
 * External references
 */
        .equ    PROC1_AS_ID,        0x00E2060A  /* Current address space ID */
        .equ    ML_EXCLUSION_START, 0x00E20DF8  /* Acquire exclusion lock */
        .equ    ML_EXCLUSION_STOP,  0x00E20E7E  /* Release exclusion lock */
        .equ    FIM_UII,            0x00E2146C  /* Unimplemented instruction handler */
        .equ    FIM_EXIT,           0x00E228BC  /* Return from exception (RTE) */

/*
 * FIM_$FLINE - F-Line exception handler
 *
 * Implements lazy FPU context switching:
 *   1. If no FPU present (FP_$SAVEP == 0), forward to UII
 *   2. If current AS is 0 (no process), forward to UII
 *   3. If current AS already owns FPU, forward to UII
 *      (instruction is truly unimplemented)
 *   4. Otherwise, acquire exclusion lock, switch FP owner,
 *      release lock, and retry the instruction via RTE
 *
 * Registers saved/restored: D0-D3, A0-A1
 *
 * Assembly (0x00E21ACC):
 */
        .global FIM_$FLINE
FIM_$FLINE:
        movem.l %d0-%d3/%a0-%a1,-(%sp)         /* Save registers */
        move.l  (FP_$SAVEP,%pc),%d0             /* d0 = FP_$SAVEP */
        beq.b   .no_fpu                         /* No FPU -> UII */
        move.w  (PROC1_AS_ID).l,%d2             /* d2 = current AS ID */
        beq.b   .no_fpu                         /* AS 0 -> UII */
        cmp.w   (FP_$OWNER,%pc),%d2             /* Compare with FP owner */
        beq.b   .no_fpu                         /* Same owner -> UII */
        pea     (fp_exclusion,%pc)              /* Push exclusion lock ptr */
        jsr     (ML_EXCLUSION_START).l          /* Acquire exclusion */
        addq.l  #4,%sp
        jsr     (fp_$switch_owner_d2).l         /* Switch FP context (d2 has AS ID) */
        pea     (fp_exclusion,%pc)              /* Push exclusion lock ptr */
        jsr     (ML_EXCLUSION_STOP).l           /* Release exclusion */
        addq.l  #4,%sp
        movem.l (%sp)+,%d0-%d3/%a0-%a1          /* Restore registers */
        jmp     (FIM_EXIT).l                    /* RTE - retry FPU instruction */
.no_fpu:
        movem.l (%sp)+,%d0-%d3/%a0-%a1          /* Restore registers */
        jmp     (FIM_UII).l                     /* Forward to UII handler */

/*
 * Module-local data (PC-relative from FIM_$FLINE)
 *
 * In the original ROM, these were located at:
 *   FP_$SAVEP    0x00E218D0 - uint32: non-zero if FPU hardware is present
 *   FP_$OWNER    0x00E218D4 - uint16: AS ID of current FPU owner
 *   fp_exclusion 0x00E218D6 - ml_$exclusion_t: FP exclusion lock
 *
 * These are accessed PC-relative by FIM_$FLINE above, so they must
 * be in the same section. They are also accessed by other FP routines
 * (fp_$switch_owner, FIM_$FP_INIT, etc.) via their global labels.
 */
        .global FP_$SAVEP
FP_$SAVEP:
        .long   0                               /* Non-zero if FPU present */

        .global FP_$OWNER
FP_$OWNER:
        .word   0                               /* AS ID of current FPU owner */

        .global fp_exclusion
fp_exclusion:
        .long   0                               /* ml_$exclusion_t */

        .end
