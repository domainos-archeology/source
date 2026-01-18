/*
 * fp/fp_internal.h - FP Internal Declarations
 *
 * Internal data structures and helper function declarations
 * for the floating point context management subsystem.
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

/* FP exclusion lock address */
#define FP_EXCLUSION_ADDR       0x00E218D6

/* FP owner address */
#define FP_OWNER_ADDR           0x00E218D4

/* Hardware FP owner register (on some SAU2 hardware) */
#define FP_HW_OWNER_ADDR        0x00FFB402

/* FP save pending flag address */
#define FP_SAVEP_ADDR           0x00E218D0

/* FP save area base address
 * Indexed by: base + (asid * FP_SAVE_AREA_SIZE)
 */
#define FP_SAVE_AREA_BASE       0x00E21928

/* FPCR default value (no exceptions enabled, round to nearest) */
#define FP_DEFAULT_FPCR         0x0000F400

/*
 * ============================================================================
 * FP State Structure
 * ============================================================================
 *
 * The FP save area for each address space contains:
 *
 * When FP state is saved with full registers (flags == 0xFFFF):
 *   -0x6A: Flags (0xFFFF = has full state)
 *   -0x68: FPCR, FPSR, FPIAR (12 bytes)
 *   -0x5C: FP0-FP7 (96 bytes, 12 bytes each in extended precision)
 *   -0x04: FSAVE state (variable size, depends on FPU state)
 *
 * When FP state is saved with internal state only (flags == 0x0000):
 *   -0x04: FSAVE state only
 *
 * The pointer stored at (base + asid*0x14A - 4) points to the
 * current top of the saved state.
 */

typedef struct fp_save_area_t {
    uint16_t    flags;          /* 0xFFFF if full state, 0 if internal only */
    uint32_t    fpcr;           /* FP control register */
    uint32_t    fpsr;           /* FP status register */
    uint32_t    fpiar;          /* FP instruction address register */
    uint8_t     fp_regs[96];    /* FP0-FP7 in extended precision (12 bytes each) */
    uint8_t     internal[184];  /* FSAVE internal state (max size for 68882) */
} fp_save_area_t;

/*
 * ============================================================================
 * Internal Function Prototypes (Assembly)
 * ============================================================================
 */

/*
 * fp_$switch_owner - Switch FP owner and restore state
 *
 * Sets PROC1_$AS_ID as the new FP owner. If the new owner
 * is different from the current owner, saves the current
 * owner's state and restores the new owner's state.
 *
 * This is the main entry point for FP context switching.
 *
 * Original address: 0x00E21B10 (6 bytes entry, falls through)
 */
void fp_$switch_owner(void);

/*
 * fp_$switch_owner_d2 - Switch FP owner with AS ID in D2
 *
 * Alternate entry point to fp_$switch_owner where the caller
 * has already loaded the desired AS ID into register D2.
 *
 * This bypasses the PROC1_$AS_ID load at the start of
 * fp_$switch_owner, allowing callers to switch to a specific
 * AS rather than the current one.
 *
 * Original address: 0x00E21B16
 */
void fp_$switch_owner_d2(void);

/*
 * fp_$check_owner - Check and set FP owner
 *
 * Checks if PROC1_$AS_ID is already the FP owner.
 * If not, sets it as owner and saves the previous owner's state.
 *
 * Does NOT restore the new owner's state (unlike fp_$switch_owner).
 *
 * Original address: 0x00E21D70 (36 bytes)
 */
void fp_$check_owner(void);

/*
 * fp_$save_state - Save FP state for an address space
 *
 * Saves the current FPU state (registers and internal state)
 * to the save area for the specified AS.
 *
 * Input:
 *   D0.w - Address space ID
 *   A1 - Base pointer for save area calculation
 *
 * Uses FSAVE to save internal state, then FMOVEM.X and FMOVEM.L
 * to save the FP registers.
 *
 * Original address: 0x00E21B5C (36 bytes)
 */
void fp_$save_state(void);

/*
 * fp_$restore_state - Restore FP state for an address space
 *
 * Restores the FPU state (registers and internal state)
 * from the save area for the specified AS.
 *
 * Input:
 *   D2.w - Address space ID
 *   A1 - Base pointer for save area calculation
 *
 * Uses FRESTORE to restore internal state, then FMOVEM.L and
 * FMOVEM.X to restore the FP registers.
 *
 * Original address: 0x00E21B30 (44 bytes)
 */
void fp_$restore_state(void);

#endif /* FP_INTERNAL_H */
