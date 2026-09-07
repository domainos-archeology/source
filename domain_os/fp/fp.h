/*
 * fp/fp.h - Floating Point Context Management Module
 *
 * Manages 68881/68882 FPU context switching between address spaces.
 * The FPU is a shared resource, so context must be saved when
 * switching between address spaces that use floating point.
 *
 * The FP subsystem maintains a "current owner" for the FPU. When
 * an address space needs to use the FPU, it must acquire ownership
 * via FP_$GET_FP. When done (typically at context switch), it
 * releases ownership via FP_$PUT_FP.
 *
 * FPU state is saved per-address space in a table whose base is the
 * pointer FP_$SAVEP, indexed by AS ID * 0x14A (330 bytes per AS).
 *
 * All FP operations are protected by an exclusion lock to ensure
 * atomic context switching.
 */

#ifndef FP_H
#define FP_H

#include "base/base.h"
#include "ml/ml.h"

/*
 * ============================================================================
 * Constants
 * ============================================================================
 */

/* Size of FP save area per address space (330 bytes) */
#define FP_SAVE_AREA_SIZE       0x14A

/*
 * A slot's layout is described in fp/fp_internal.h: the LAST longword of
 * the slot is a pointer to the top of the saved frame, and the frame is
 * written downward from there.  The slot is not a forward-laid-out record.
 */

/*
 * ============================================================================
 * Global Data
 * ============================================================================
 */

/*
 * FP_$OWNER - Current FPU owner (address space ID)
 *
 * The AS ID that currently "owns" the FPU state.
 * When another AS needs to use the FPU, the current owner's
 * state is saved and the new owner's state is restored.
 *
 * Address: 0x00E218D4
 */
extern uint16_t FP_$OWNER;

/*
 * FP_$SAVEP - base of the per-address-space FP save-area table
 *
 * This is a POINTER, not a flag (source-djly).  fp_$switch_owner_d2
 * (0x00E21B1A `movea.l (-0x24c,PC),A1`) and fp_$check_owner (0x00E21D7A)
 * load it into A1, and fp_$save_state / fp_$restore_state then address
 * an address space's slot as (-0x4,A1,asid*FP_SAVE_AREA_SIZE).
 *
 * The table is allocated at boot by PEB_$LOAD_WCS
 * (`move.l A0,(0x00e218d0).l` at 0x00E3207C) and cleared by OS_$SHUTDOWN
 * (0x00E6D52E); a zero value means "no FPU configured", which is what
 * FIM_$FP_ABORT (0x00E21B80) and FIM_$FP_INIT (0x00E21BB0) test with
 * `tst.l` before doing anything.  It is zero in the image.
 *
 * Address: 0x00E218D0
 */
extern m68k_ptr_t FP_$SAVEP;

/*
 * FP_$EXCLUSION - FPU access exclusion lock
 *
 * ML exclusion structure used to serialize FPU access.
 * All FP context operations must be done under this lock.
 *
 * Address: 0x00E218D6
 */
extern ml_$exclusion_t FP_$EXCLUSION;

/*
 * ============================================================================
 * Function Prototypes
 * ============================================================================
 */

/*
 * FP_$GET_FP - Get FPU context for address space
 *
 * Acquires the FPU for the specified address space. If the FPU
 * is currently owned by a different AS, the current state is
 * saved and the target AS's state is restored.
 *
 * This function:
 * 1. Acquires the FP exclusion lock
 * 2. Checks if current AS already owns FPU
 * 3. If not, saves current owner's state and sets new owner
 * 4. Restores the target AS's FP state
 * 5. Releases the exclusion lock
 *
 * Parameters:
 *   asid - Address space ID to get FP context for
 *
 * Note that the owner it installs is PROC1_$AS_ID (fp_$check_owner) while
 * the state it restores is the caller's `asid` argument, reloaded into D2
 * at 0x00E21D58.  The two need not be the same.
 *
 * Hand-written assembly: fp/sau2/fp_context.s.
 * Original address: 0x00E21D48 (40 bytes)
 */
void FP_$GET_FP(uint16_t asid);

/*
 * FP_$PUT_FP - Put (save) FPU context for address space
 *
 * Saves the FPU state for the specified address space.
 * Called during context switch to preserve FP state.
 *
 * This function:
 * 1. Acquires the FP exclusion lock
 * 2. Switches FP owner to current AS if needed
 * 3. Saves the target AS's FP state
 * 4. Releases the exclusion lock
 *
 * Parameters:
 *   asid - Address space ID to save FP context for
 *
 * Hand-written assembly: fp/sau2/fp_context.s.
 * Original address: 0x00E21D94 (46 bytes)
 */
void FP_$PUT_FP(uint16_t asid);

#endif /* FP_H */
