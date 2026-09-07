/*
 * fp/fp_data.c - FP Global Data
 *
 * Global variables for the floating point context management subsystem.
 */

#include "fp/fp_internal.h"

/*
 * FP_$SAVEP - base of the per-address-space FP save-area table
 *
 * A pointer, not a flag; see fp/fp.h (source-djly).  Zero means "no FPU
 * configured".
 *
 * Address: 0x00E218D0
 */
/*
 * On m68k (SAU2) FP_$SAVEP, FP_$OWNER and FP_$EXCLUSION are defined in
 * fim/sau2/fim.s: FIM_$FLINE addresses them PC-relative, so they must be
 * assembled alongside it to preserve the original layout.  The C
 * definitions below are used for other architectures.
 */
#if !defined(ARCH_M68K)

m68k_ptr_t FP_$SAVEP;

/*
 * FP_$OWNER - Current FPU owner (address space ID)
 *
 * The address space ID that currently owns the FPU.
 * Other address spaces must acquire ownership before
 * using floating point.
 *
 * Address: 0x00E218D4
 */
uint16_t FP_$OWNER;

/*
 * FP_$EXCLUSION - FPU access exclusion lock
 *
 * ML exclusion structure for serializing FPU access.
 * Must be held during all FP context operations.
 *
 * Address: 0x00E218D6
 * Size: Depends on ml_$exclusion_t structure
 */
ml_$exclusion_t FP_$EXCLUSION;

#endif /* !ARCH_M68K */
