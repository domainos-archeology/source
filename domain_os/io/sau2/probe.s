/*
 * io_$probe - Hardware probe with bus error handling
 *
 * Probes for hardware controller presence at a given address.
 * Sets up a bus error recovery handler, disables interrupts,
 * clears the MMU status register, then dispatches through a
 * jump table based on the controller type for the actual probe.
 *
 * If a bus error occurs during the probe, the recovery handler
 * restores the stack and returns non-negative (not found).
 * If the probe succeeds, returns negative (found).
 *
 * Register usage:
 *   A0 = type pointer (first word is jump table index)
 *   A1 = hardware address (dereferenced from param_2)
 *   A2 = result buffer
 *   D0 = jump table offset (type[0] * 2)
 *   D1 = saved stack pointer for bus error recovery
 *   D3 = saved status register
 *
 * Parameters (stack):
 *   param_1 = Pointer to type word (jump table index)
 *   param_2 = Pointer to hardware address pointer (double deref)
 *   param_3 = Result buffer pointer
 *
 * Returns:
 *   D0.b: negative if hardware found, non-negative if not found
 *
 * Original address: 0x00E29138
 * Size: 54 bytes (plus jump table targets)
 */

        .text
        .even

        .extern BUS_ERROR_SWITCH
        .extern MMU_STATUS_REG

        .global io_$probe
io_$probe:
        movem.l %d3/%a2/%a4, -(%sp)

        /* Load parameters from stack (after register save) */
        movea.l (0x10,%sp), %a0         /* A0 = param_1 (type pointer) */
        move.w  (%a0), %d0              /* D0.w = type[0] (jump table index) */
        lsl.w   #1, %d0                 /* D0.w *= 2 (word offset) */

        movea.l (0x14,%sp), %a1         /* A1 = param_2 */
        movea.l (%a1), %a1              /* A1 = *param_2 (hw address) */

        movea.l (0x18,%sp), %a2         /* A2 = param_3 (result buffer) */

        /* Save SR and raise IPL to 7 (disable all interrupts) */
        move    %sr, %d3
        ori     #0x0700, %sr

        /* Set bus error recovery handler */
        lea     .Lbus_error_recovery(%pc), %a0
        move.l  %a0, BUS_ERROR_SWITCH

        /* Save SP for recovery */
        move.l  %sp, %d1

        /* Clear MMU status register */
        clr.b   MMU_STATUS_REG

        /* Dispatch through jump table based on type */
        lea     .Ljump_table(%pc), %a0
        jmp     (0,%a0,%d0.w)

.Ljump_table:
        /* TODO: Jump table entries for different probe types.
         * The targets perform the actual hardware access and set D0
         * based on whether the device responded.
         * Each entry is a 2-byte relative branch.
         *
         * Full jump table reconstruction requires analyzing the
         * code immediately following the original function at 0xe2916e.
         */
        bra.w   .Lbus_error_recovery    /* Type 0: placeholder */

.Lbus_error_recovery:
        /* Bus error or probe complete - restore state */
        move    %d3, %sr                /* Restore interrupt level */
        movem.l (%sp)+, %d3/%a2/%a4
        rts
