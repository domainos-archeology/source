/*
 * ACL_$USED_SUSER - Check if current process has used superuser privilege
 *
 * Returns non-zero if the current process has used superuser privilege
 * at any point (tracked in the ASID_SUSER_BITMAP).
 *
 * This is used for auditing - even if a process no longer has superuser
 * status, this function can tell if it ever did.
 *
 * Returns:
 *   Non-zero (-1) if superuser privilege was used, 0 otherwise
 *
 * Original address: 0x00E492CC
 */

#include "acl/acl_internal.h"

int8_t ACL_$USED_SUSER(void)
{
    int16_t pid = PROC1_$CURRENT;
    int16_t bit_index = pid - 1;
    int16_t byte_index = bit_index >> 3;
    int16_t bit_offset = 7 - (bit_index & 7);

    if (ACL_$ASID_SUSER_BITMAP[byte_index] & (1 << bit_offset)) {
        return -1;  /* 0xFF */
    }
    return 0;
}
