/*
 * peb/test_parity_err.c - PEB Parity Error Test
 *
 * Tests for parity errors in the PEB hardware.
 *
 * Original address: 0x00E11FF4 (54 bytes)
 */

#include "peb/peb_internal.h"

/*
 * PEB_$TEST_PARITY_ERR - Test for PEB parity errors
 *
 * Checks if a parity error has occurred in the PEB hardware.
 * The PEB hardware sets bit 1 of the status byte (0xFF7001) when
 * a parity error is detected.
 *
 * Assembly analysis:
 *   00e11ff4    link.w A6,0x0
 *   00e11ff8    pea (A5)
 *   00e11ffa    lea (0xe24c78).l,A5       ; PEB globals base
 *   00e12000    movea.l (0x8,A6),A0       ; param_1 (status pointer)
 *   00e12004    tst.b (0x1a,A5)           ; Test PEB_$INSTALLED
 *   00e12008    bpl.b 0x00e1201c          ; If not installed, no error
 *   00e1200a    btst.b #0x1,(0x00ff7001).l ; Test parity error bit
 *   00e12012    beq.b 0x00e1201c          ; If not set, no error
 *   00e12014    move.l #0x12001b,(A0)     ; status = parity error
 *   00e1201a    bra.b 0x00e12022
 *   00e1201c    move.l #0x12000f,(A0)     ; status = no parity error
 *   00e12022    movea.l (-0x4,A6),A5
 *   00e12026    unlk A6
 *   00e12028    rts
 *
 * Parameters:
 *   status - Pointer to receive status code
 *            status_$peb_parity_error (0x12001B) if error detected
 *            status_$peb_no_parity_error (0x12000F) if no error
 */
void PEB_$TEST_PARITY_ERR(status_$t *status)
{
    /* Check if PEB is installed and parity error bit is set */
    if ((PEB_$INSTALLED < 0) && ((PEB_STATUS_BYTE & PEB_STATUS_PARITY_ERR) != 0)) {
        /* Parity error detected */
        *status = status_$peb_parity_error;
    } else {
        /* No parity error (or PEB not installed) */
        *status = status_$peb_no_parity_error;
    }
}
