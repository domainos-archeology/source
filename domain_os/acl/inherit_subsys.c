/*
 * ACL_$INHERIT_SUBSYS - Inherit subsystem state from parent process
 *
 * Copies the subsystem inheritance flag bit from the specified source
 * to the current process's ASID state bitmap.
 *
 * Parameters:
 *   inherit_flag - Pointer to byte containing inheritance flag (bit masked)
 *   status_ret   - Output status code (always set to 0)
 *
 * Original address: 0x00E49138
 */

#include "acl/acl_internal.h"

void ACL_$INHERIT_SUBSYS(uint8_t *inherit_flag, status_$t *status_ret)
{
    int16_t pid = PROC1_$CURRENT;
    int16_t asid_index = pid - 1;  /* ASID indices are 0-based, PIDs are 1-based */
    int16_t byte_index;
    uint8_t bit_mask;

    /* Calculate byte index and bit mask for this ASID */
    byte_index = asid_index >> 3;       /* Divide by 8 */
    bit_mask = 0x80 >> (asid_index & 7); /* Bit position within byte */

    /* Copy the inheritance bit from source to destination */
    /* Clear the target bit, then OR in the source bit if set */
    ACL_$ASID_FREE_BITMAP[byte_index] =
        (ACL_$ASID_FREE_BITMAP[byte_index] & ~bit_mask) |
        (*inherit_flag & bit_mask);

    *status_ret = status_$ok;
}
