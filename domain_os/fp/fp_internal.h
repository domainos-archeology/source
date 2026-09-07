/*
 * fp/fp_internal.h - FP Internal Declarations
 *
 * Internal data structures and helper declarations for the 68881/68882
 * context-management subsystem.
 *
 * Every routine below is hand-written assembly in the image: they take
 * their arguments in registers, fall through into one another and rely on
 * A1 surviving across calls.  They are transcribed in
 * fp/sau2/fp_context.s, and the prototypes here exist only so that C
 * callers in other subsystems have a declaration to reference.
 */

#ifndef FP_INTERNAL_H
#define FP_INTERNAL_H

#include "fp/fp.h"
#include "ml/ml.h"
#include "proc1/proc1.h"

/*
 * ============================================================================
 * Internal Constants
 * ============================================================================
 */

/* Addresses of the FP module's data cells (see fim/sau2/fim.s, which
 * defines them because FIM_$FLINE reaches them PC-relative). */
#define FP_SAVEP_ADDR           0x00E218D0  /* longword: save-area base ptr */
#define FP_OWNER_ADDR           0x00E218D4  /* word: owning AS id */
#define FP_EXCLUSION_ADDR       0x00E218D6  /* ml_$exclusion_t, 18 bytes */

/*
 * Hardware FPU-owner register.  fp_$switch_owner_d2 (0x00E21B28) and
 * fp_$check_owner (0x00E21D88) mirror the low byte of the owning AS id
 * here, and FIM_$FP_INIT (0x00E21BDE) clears it.
 */
#define FP_HW_OWNER_ADDR        0x00FFB402

/*
 * FPCR loaded for an address space that has no saved frame
 * (0x00E21B3E `fmove.l #0xf400,FPCR`): extended precision, round to
 * nearest, every exception trap disabled.
 */
#define FP_DEFAULT_FPCR         0x0000F400

/*
 * ============================================================================
 * FP save area (source-djly)
 * ============================================================================
 *
 * There is NO fixed save-area address in the image.  FP_$SAVEP
 * (0x00E218D0) is a longword POINTER to the per-address-space table; it
 * is allocated at boot by PEB_$LOAD_WCS (`move.l A0,(0x00e218d0).l` at
 * 0x00E3207C) and cleared by OS_$SHUTDOWN (0x00E6D52E).  A zero value
 * means "no FPU configured", which is exactly what FIM_$FP_ABORT
 * (0x00E21B80 `move.l (-0x2b2,PC),D0` / `beq`) and FIM_$FP_INIT
 * (0x00E21BB0 `tst.l (0x00e218d0).l` / `beq`) test.
 *
 * (The previous FP_SAVE_AREA_BASE 0x00E21928 was inside FIM_$BUS_ERR --
 * the extension words of the `cmpi.b #0xA0,(0x22,SP)` at 0x00E21926 --
 * and has been removed.)
 *
 * Slot addressing, from fp_$save_state / fp_$restore_state:
 *
 *   00e21b5c  mulu.w #0x14a,D0             ; byte offset of the slot
 *   00e21b60  beq -> rts                   ; asid 0 = no address space
 *   00e21b62  lea (-0x4,A1,D0*0x1),A0      ; A0 = &slot->state
 *
 * so a slot is FP_SAVE_AREA_SIZE (0x14A) bytes and its LAST longword
 * holds the pointer to the top of the saved frame.  The frame is written
 * DOWNWARD from that cell:
 *
 *   state -> [0xFFFF][FPCR FPSR FPIAR (12)][FP0..FP7 (96)][FSAVE frame]
 *
 * when the FSAVE frame is not null (`tst.b (0x1,A0)` at 0x00E21B68 tests
 * the 68881/68882 frame-size byte), and
 *
 *   state -> [FSAVE frame]
 *
 * when it is.  The 0xFFFF marker is what fp_$restore_state skips with
 * `addq.w #2,A0` at 0x00E21B4E before reloading the registers.  326 bytes
 * (0x14A - 4) are available below the cell, which is exactly the 2 + 12 +
 * 96 bytes of register state plus the 216-byte maximum 68882 FSAVE frame.
 */

typedef struct __attribute__((packed)) fp_save_area_t {
    /*
     * 0x000: the saved frame, filled downward from the end of this array.
     * `state` below points at its first live byte; the bytes before that
     * point are stale.
     */
    uint8_t     frame[FP_SAVE_AREA_SIZE - 4];

    /*
     * 0x146: pointer to the top of the frame above, or 0 if this address
     * space has never had its FP state saved (FIM_$FP_INIT clears it with
     * `clr.l (-0x4,A0,D1w*0x1)` at 0x00E21BFE).
     */
    m68k_ptr_t  state;
} fp_save_area_t;

_Static_assert(__builtin_offsetof(fp_save_area_t, frame) == 0,
               "fp_save_area_t.frame must be at 0x00");
_Static_assert(__builtin_offsetof(fp_save_area_t, state) == FP_SAVE_AREA_SIZE - 4,
               "fp_save_area_t.state must be the slot's last longword");
_Static_assert(sizeof(fp_save_area_t) == FP_SAVE_AREA_SIZE,
               "fp_save_area_t must be 0x14A bytes (mulu.w #0x14a)");

/* Bytes of frame available below a slot's state cell. */
#define FP_FRAME_MAX            (FP_SAVE_AREA_SIZE - 4)

/* Word written in front of the register block when registers were saved
 * (0x00E21B76 `move.w #-1,-(A0)`). */
#define FP_STATE_MARKER         0xFFFF

/*
 * ============================================================================
 * Internal routines (fp/sau2/fp_context.s)
 * ============================================================================
 *
 * None of these follows the C calling convention; the declarations are
 * `void (void)` so that C sources can name them, and the register
 * contracts are documented per routine.
 */

/*
 * fp_$switch_owner - 0x00E21B10 (6 bytes, falls through)
 *
 * Loads D2 = PROC1_$AS_ID and falls into fp_$switch_owner_d2.
 */
void fp_$switch_owner(void);

/*
 * fp_$switch_owner_d2 - 0x00E21B16 (26 bytes, falls through)
 *
 * In:  D2.w = desired AS id
 * Out: A1 = FP_$SAVEP
 *
 * If D2 is already FP_$OWNER the routine returns.  Otherwise it makes D2
 * the owner (memory and FP_HW_OWNER_ADDR), calls fp_$save_state with
 * D0 = the PREVIOUS owner, then falls through into fp_$restore_state.
 * Reached from FIM_$FLINE (`bsr` at 0x00E21AF0).
 */
void fp_$switch_owner_d2(void);

/*
 * fp_$check_owner - 0x00E21D70 (36 bytes)
 *
 * Out: A1 = FP_$SAVEP
 *
 * fp_$switch_owner without the fall-through: it takes ownership for
 * PROC1_$AS_ID and saves the previous owner's state, but does not restore
 * anything.
 */
void fp_$check_owner(void);

/*
 * fp_$save_state - 0x00E21B5C (36 bytes)
 *
 * In: D0.w = asid, A1 = FP_$SAVEP
 *
 * `fsave -(A0)` from the end of the slot, then, if the frame is not null,
 * FP0-FP7, FPCR/FPSR/FPIAR and the 0xFFFF marker; finally stores the
 * frame pointer in the slot's last longword.  asid 0 is a no-op.
 */
void fp_$save_state(void);

/*
 * fp_$restore_state - 0x00E21B30 (44 bytes)
 *
 * In: D2.w = asid, A1 = FP_$SAVEP
 *
 * Reloads the slot's frame with `frestore (A0)+`, preceded by the
 * register reload when the frame is not null.  With no saved frame it
 * only sets FPCR to FP_DEFAULT_FPCR.  asid 0 is a no-op.
 */
void fp_$restore_state(void);

#endif /* FP_INTERNAL_H */
