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
 * FPU state is saved per-address space in a save area indexed by
 * AS ID * 0x14A (330 bytes per AS).
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

/* FP state save area contents:
 *   Offset 0x00: State pointer (4 bytes)
 *   Offset 0x04: Flags/format (2 bytes)
 *   Offset 0x06: FPCR (4 bytes)
 *   Offset 0x0A: FPSR (4 bytes)
 *   Offset 0x0E: FPIAR (4 bytes)
 *   Offset 0x12: FP0-FP7 (8 * 12 = 96 bytes extended precision)
 *   Offset 0x72: Internal state from FSAVE (variable, up to 184 bytes)
 */

/* FP state flags */
#define FP_STATE_VALID          0xFFFF  /* State is valid and has FP regs */
#define FP_STATE_INTERNAL_ONLY  0x0000  /* Only internal state, no regs */

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
 * FP_$SAVEP - FP save pending flag
 *
 * Non-zero if an FP save is pending (the FPU state needs
 * to be saved before switching owners).
 *
 * Address: 0x00E218D0
 */
extern uint32_t FP_$SAVEP;

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
 * Original address: 0x00E21D94 (46 bytes)
 */
void FP_$PUT_FP(uint16_t asid);

#endif /* FP_H */
