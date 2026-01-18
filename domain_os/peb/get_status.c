/*
 * peb/get_status.c - Get PEB Exception Status
 *
 * Returns a status code indicating the type of floating point
 * exception that occurred.
 *
 * Original address: 0x00E5ADCA (150 bytes)
 */

#include "peb/peb_internal.h"

/*
 * PEB_$GET_STATUS - Get PEB exception status
 *
 * Reads the PEB exception status register and returns a status
 * code indicating the type of exception. The status register is
 * at offset 0xF4 from the PEB base address.
 *
 * The function checks bits in priority order:
 *   Bit 0: Overflow
 *   Bit 1: Underflow
 *   Bit 2: Division by zero
 *   Bit 3: Loss of significance
 *   Bit 4: Hardware error
 *   Bit 5: Unimplemented opcode
 *
 * If no specific exception bits are set, returns status_$peb_interrupt.
 *
 * Assembly analysis:
 *   00e5adca    link.w A6,-0x10
 *   00e5adce    tst.b (0x00e24c97).l     ; Test mmu_installed flag
 *   00e5add4    bmi.b 0x00e5adde         ; If installed, use 0xFF7400
 *   00e5add6    movea.l #0x7000,A0       ; else use base 0x7000
 *   00e5addc    bra.b 0x00e5ade4
 *   00e5adde    movea.l #0xff7400,A0     ; Private mirror at 0xFF7400
 *   00e5ade4    lea (0xf4,A0),A0         ; Status at offset 0xF4
 *   00e5ade8    move.l (A0),D0           ; Read status
 *   00e5adea    btst.l #0x0,D0           ; Test overflow
 *   00e5adee    beq.b 0x00e5adfa
 *   00e5adf0    move.l #0x240003,(-0x8,A6) ; status_$peb_fp_overflow
 *   00e5adf8    bra.b 0x00e5ae52
 *   ... (similar tests for other bits)
 *   00e5ae4a    move.l #0x240002,(-0x8,A6) ; status_$peb_interrupt (default)
 *   00e5ae52    move.l (-0x8,A6),(-0x10,A6)
 *   00e5ae58    move.l (-0x10,A6),D0     ; Return value in D0
 *   00e5ae5c    unlk A6
 *   00e5ae5e    rts
 */
status_$t PEB_$GET_STATUS(void)
{
    uint32_t exc_status;
    volatile uint32_t *status_reg;

    /* Select register address based on MMU installed state */
    if (PEB_$MMU_INSTALLED < 0) {
        /* Use private mirror address */
        status_reg = (volatile uint32_t *)(0xFF7400 + PEB_STATUS_OFFSET);
    } else {
        /* Use base address */
        status_reg = (volatile uint32_t *)(0x7000 + PEB_STATUS_OFFSET);
    }

    /* Read the exception status */
    exc_status = *status_reg;

    /* Check exception bits in priority order */
    if (exc_status & PEB_EXC_OVERFLOW) {
        return status_$peb_fp_overflow;
    }

    if (exc_status & PEB_EXC_UNDERFLOW) {
        return status_$peb_fp_underflow;
    }

    if (exc_status & PEB_EXC_DIV_BY_ZERO) {
        return status_$peb_div_by_zero;
    }

    if (exc_status & PEB_EXC_LOSS_SIG) {
        return status_$peb_fp_loss_of_significance;
    }

    if (exc_status & PEB_EXC_HW_ERROR) {
        return status_$peb_fp_hw_error;
    }

    if (exc_status & PEB_EXC_UNIMP_OPCODE) {
        return status_$peb_unimplemented_opcode;
    }

    /* No specific exception - return generic interrupt status */
    return status_$peb_interrupt;
}
