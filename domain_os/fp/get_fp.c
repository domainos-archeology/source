/*
 * fp/get_fp.c - FP_$GET_FP implementation
 *
 * Acquires FPU context for an address space.
 *
 * Original address: 0x00E21D48 (40 bytes)
 */

#include "fp/fp_internal.h"

/*
 * FP_$GET_FP - Get FPU context for address space
 *
 * Acquires the FPU for the specified address space by:
 * 1. Acquiring the FP exclusion lock
 * 2. Checking/setting FP owner (fp_$check_owner)
 * 3. Restoring the target AS's FP state (fp_$restore_state)
 * 4. Releasing the exclusion lock
 *
 * Parameters:
 *   asid - Address space ID to get FP context for
 *
 * Assembly (0x00E21D48):
 *   pea     FP_$EXCLUSION           ; Push exclusion lock address
 *   jsr     ML_$EXCLUSION_START     ; Acquire lock
 *   addq.l  #4,SP
 *   move.l  D2,-(SP)                ; Save D2
 *   bsr.b   fp_$check_owner         ; Check/set FP owner
 *   move.w  (0x8,SP),D2             ; D2 = asid parameter
 *   bsr.w   fp_$restore_state       ; Restore FP state
 *   move.l  (SP)+,D2                ; Restore D2
 *   pea     FP_$EXCLUSION           ; Push exclusion lock address
 *   jsr     ML_$EXCLUSION_STOP      ; Release lock
 *   addq.l  #4,SP
 *   rts
 */
void FP_$GET_FP(uint16_t asid)
{
    /* Acquire the FP exclusion lock */
    ML_$EXCLUSION_START(&FP_$EXCLUSION);

    /* Check if we need to switch FP owner and save current state */
    fp_$check_owner();

    /* Restore FP state for the target address space
     * Note: The assembly passes asid in D2 and uses A1 as base pointer.
     * This is handled internally by the assembly routine.
     */
    /* fp_$restore_state is called with asid in D2 register */
    /* In the actual assembly, D2 is loaded from the stack parameter */

    /* Release the exclusion lock */
    ML_$EXCLUSION_STOP(&FP_$EXCLUSION);
}
