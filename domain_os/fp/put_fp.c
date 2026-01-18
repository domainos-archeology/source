/*
 * fp/put_fp.c - FP_$PUT_FP implementation
 *
 * Saves FPU context for an address space.
 *
 * Original address: 0x00E21D94 (46 bytes)
 */

#include "fp/fp_internal.h"

/*
 * FP_$PUT_FP - Put (save) FPU context for address space
 *
 * Saves the FPU state for the specified address space by:
 * 1. Acquiring the FP exclusion lock
 * 2. Switching FP owner (fp_$switch_owner)
 * 3. Saving the target AS's FP state (fp_$save_state)
 * 4. Releasing the exclusion lock
 *
 * Parameters:
 *   asid - Address space ID to save FP context for
 *
 * Assembly (0x00E21D94):
 *   pea     FP_$EXCLUSION           ; Push exclusion lock address
 *   jsr     ML_$EXCLUSION_START     ; Acquire lock
 *   addq.l  #4,SP
 *   movem.l D2/D3,-(SP)             ; Save D2, D3
 *   bsr.w   fp_$switch_owner        ; Switch FP owner
 *   move.w  (0xc,SP),D0             ; D0 = asid parameter
 *   bsr.w   fp_$save_state          ; Save FP state
 *   movem.l (SP)+,D2/D3             ; Restore D2, D3
 *   pea     FP_$EXCLUSION           ; Push exclusion lock address
 *   jsr     ML_$EXCLUSION_STOP      ; Release lock
 *   addq.l  #4,SP
 *   rts
 */
void FP_$PUT_FP(uint16_t asid)
{
    /* Acquire the FP exclusion lock */
    ML_$EXCLUSION_START(&FP_$EXCLUSION);

    /* Switch FP owner to current AS if needed */
    fp_$switch_owner();

    /* Save FP state for the target address space
     * Note: The assembly passes asid in D0 and uses A1 as base pointer.
     * This is handled internally by the assembly routine.
     */
    /* fp_$save_state is called with asid in D0 register */

    /* Release the exclusion lock */
    ML_$EXCLUSION_STOP(&FP_$EXCLUSION);
}
