/*
 * fp/fp_data.c - FP Global Data
 *
 * Global variables for the floating point context management subsystem.
 */

#include "fp/fp_internal.h"

/*
 * FP_$SAVEP - FP save pending flag
 *
 * Non-zero if the current FPU state needs to be saved
 * before switching to a new owner.
 *
 * Address: 0x00E218D0
 */
uint32_t FP_$SAVEP;

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
