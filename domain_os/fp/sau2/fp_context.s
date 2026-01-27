/*
 * fp/sau2/fp_context.s - FP Context Switch Stubs (m68k/SAU2/68010)
 *
 * The 68010 does not have built-in FPU support (no 68881/68882).
 * These stubs provide no-op implementations of the FP context
 * switching routines so that linking succeeds on 68010 builds.
 *
 * For builds targeting 68020+ with FPU, replace with the full
 * implementation using fsave/frestore/fmovem instructions.
 *
 * Original addresses:
 *   fp_$switch_owner:     0x00E21B10
 *   fp_$switch_owner_d2:  0x00E21B16
 *   fp_$check_owner:      0x00E21D70
 *   fp_$save_state:       0x00E21B5C
 *   fp_$restore_state:    0x00E21B30
 */

        .text
        .even

/*
 * fp_$switch_owner - Switch FP owner and restore state (stub)
 *
 * On 68010 without FPU, this is a no-op.
 */
        .global fp_$switch_owner
fp_$switch_owner:
        rts

/*
 * fp_$switch_owner_d2 - Switch FP owner with AS ID in D2 (stub)
 *
 * On 68010 without FPU, this is a no-op.
 */
        .global fp_$switch_owner_d2
fp_$switch_owner_d2:
        rts

/*
 * fp_$check_owner - Check and set FP owner (stub)
 *
 * On 68010 without FPU, this is a no-op.
 */
        .global fp_$check_owner
fp_$check_owner:
        rts

/*
 * fp_$save_state - Save FP state for an address space (stub)
 *
 * On 68010 without FPU, this is a no-op.
 */
        .global fp_$save_state
fp_$save_state:
        rts

/*
 * fp_$restore_state - Restore FP state for an address space (stub)
 *
 * On 68010 without FPU, this is a no-op.
 */
        .global fp_$restore_state
fp_$restore_state:
        rts

        .end
