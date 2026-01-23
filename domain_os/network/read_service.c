/*
 * NETWORK_$READ_SERVICE - Read network service configuration
 *
 * Returns the current network service configuration. The return format
 * depends on whether the extended service info flag is set.
 *
 * Original address: 0x00E71D7C
 *
 * Assembly analysis:
 *   00e71d7c    link.w A6,0x0
 *   00e71d80    movea.l #0xe248fc,A1      ; Network data base address
 *   00e71d86    movea.l (0x8,A6),A0       ; A0 = result_ptr
 *   00e71d8a    btst.b #0x2,(0x343,A1)    ; Test bit 2 of byte at +0x343
 *                                         ; (bit 18 of 32-bit at +0x342)
 *   00e71d90    beq.b 0x00e71d98          ; If not set, branch
 *   00e71d92    move.l (0x342,A1),(A0)    ; Return full 32-bit allowed service
 *   00e71d96    bra.b 0x00e71da0
 *   00e71d98    clr.w (A0)                ; High word = 0
 *   00e71d9a    move.w (0x344,A1),(0x2,A0); Low word = remote pool
 *   00e71da0    unlk A6
 *   00e71da2    rts
 *
 * The function checks bit 18 (0x40000) of NETWORK_$ALLOWED_SERVICE:
 * - If set: returns the full allowed service bitmap
 * - If not set: returns (0 << 16) | NETWORK_$REMOTE_POOL
 */

#include "network/network.h"

void NETWORK_$READ_SERVICE(uint32_t *result_ptr)
{
    /*
     * Check if extended service info flag (bit 18) is set.
     * If set, return the full 32-bit allowed service bitmap.
     * Otherwise, return the remote pool value in the low word.
     */
    if ((NETWORK_$ALLOWED_SERVICE & NETWORK_SERVICE_EXTENDED) != 0) {
        *result_ptr = NETWORK_$ALLOWED_SERVICE;
    } else {
        /*
         * Return 0 in high word, remote pool in low word.
         * Original code uses separate word writes for big-endian layout.
         */
        *result_ptr = (uint32_t)NETWORK_$REMOTE_POOL;
    }
}
