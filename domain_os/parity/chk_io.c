/*
 * PARITY_$CHK_IO - Check if I/O address matches parity error
 *
 * Called by I/O subsystems to check whether their buffer addresses
 * were involved in a parity error during DMA.
 *
 * Original address: 0x00E0B174
 * Size: 72 bytes
 */

#include "parity/parity_internal.h"

/*
 * PARITY_$CHK_IO
 *
 * Parameters:
 *   ppn1 - First physical page number to check
 *   ppn2 - Second physical page number to check
 *
 * Returns:
 *   0: Neither address matches the parity error (high 16 bits of ppn2)
 *   1: First address (ppn1) matches
 *   2: Second address (ppn2) matches
 *
 * If a match is found, the parity error state is cleared.
 */
uint32_t PARITY_$CHK_IO(uint32_t ppn1, uint32_t ppn2)
{
    uint32_t result;

    /*
     * Check if there's a pending parity error during DMA.
     * The DMA flag is stored at offset 0x11 in the error status word
     * (bit 3 = 0x08 of the low byte at offset 0x11).
     */
    if ((PARITY_$ERR_STATUS & 0x08) == 0) {
        /* No DMA parity error pending - return high word of ppn2 (usually 0) */
        result = ppn2 & 0xFFFF0000;
        goto done;
    }

    /*
     * Check if first PPN matches the error location.
     */
    if (ppn1 == PARITY_$ERR_PPN) {
        result = 1;
        goto clear_and_done;
    }

    /*
     * Check if second PPN matches the error location.
     */
    if (ppn2 == PARITY_$ERR_PPN) {
        result = 2;
        goto clear_and_done;
    }

    /* Neither matches */
    result = ppn2 & 0xFFFF0000;
    goto done;

clear_and_done:
    /*
     * A match was found. Clear the parity error state so that
     * subsequent calls don't report the same error.
     */
    PARITY_$ERR_PPN = 0;
    PARITY_$DURING_DMA = 0;

done:
    return result;
}
