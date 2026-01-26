/*
 * fim/sau2/fp.s - FIM Floating Point Stubs (m68k/SAU2/68010)
 *
 * The 68010 does not have built-in FPU support. While some systems
 * used external 68881 FPUs, this SAU2 build targets the base 68010.
 * These stubs provide no-op implementations of the FP management
 * functions.
 *
 * For builds targeting 68020+ with FPU, use a full implementation.
 *
 * TODO: Implement runtime FPU detection and full FP support for
 *       systems with 68881/68882 coprocessors.
 */

        .text
        .even

/*
 * FIM_$FP_ABORT - Abort floating point operation (stub)
 *
 * On 68010 without FPU, this is a no-op.
 */
        .global FIM_$FP_ABORT
FIM_$FP_ABORT:
        rts

/*
 * FIM_$FP_INIT - Initialize FPU state (stub)
 *
 * On 68010 without FPU, this is a no-op.
 */
        .global FIM_$FP_INIT
FIM_$FP_INIT:
        rts

/*
 * FIM_$FSAVE - Save FPU state (stub)
 *
 * On 68010 without FPU, this is a no-op.
 */
        .global FIM_$FSAVE
FIM_$FSAVE:
        rts

/*
 * FIM_$FRESTORE - Restore FPU state (stub)
 *
 * On 68010 without FPU, this is a no-op.
 */
        .global FIM_$FRESTORE
FIM_$FRESTORE:
        rts

/*
 * FIM_$FP_GET_STATE - Get FPU state (stub)
 *
 * On 68010 without FPU, returns status = not available.
 */
        .global FIM_$FP_GET_STATE
FIM_$FP_GET_STATE:
        moveq   #-1,%d0                 /* Return -1 (not available) */
        rts

/*
 * FIM_$FP_PUT_STATE - Put FPU state (stub)
 *
 * On 68010 without FPU, this is a no-op.
 */
        .global FIM_$FP_PUT_STATE
FIM_$FP_PUT_STATE:
        rts

        .end
