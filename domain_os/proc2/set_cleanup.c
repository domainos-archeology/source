/*
 * PROC2_$SET_CLEANUP - Set cleanup flag bit
 *
 * Sets a bit in the current process's cleanup_flags field.
 * Each bit represents a different cleanup handler to call on process exit.
 * Only operates if address space is valid (PROC1_$AS_ID != 0).
 *
 * Parameters:
 *   bit_num - The bit number (0-15) to set in cleanup_flags
 *
 * Original address: 0x00e41572
 */

#include "proc2.h"

void PROC2_$SET_CLEANUP(uint16_t bit_num)
{
    uint16_t index;
    proc2_info_t *info;

    /* Only proceed if address space is valid */
    if (PROC1_$AS_ID != 0) {
        /* Get current process's PROC2 index */
        index = P2_PID_TO_INDEX(PROC1_$CURRENT);

        /* Get pointer to process info entry */
        info = P2_INFO_ENTRY(index);

        /* Set the specified bit in cleanup_flags */
        /* Mask to 5 bits (0-31) for safety, though field is 16 bits */
        info->cleanup_flags |= (uint16_t)(1 << (bit_num & 0x1f));
    }
}
