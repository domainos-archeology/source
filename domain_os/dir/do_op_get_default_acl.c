/*
 * dir_$do_op_get_default_acl - Server-side handler for GET_DEFAULT_ACL op
 *
 * Server-side handler for opcode 0x4E in DIR_$DO_OP. Retrieves the default
 * ACL UID for a directory. Opens the directory with read access, checks the
 * requested ACL type against ACL_$DIR_ACL and ACL_$FILE_ACL, and returns
 * the appropriate UID with type bits set.
 *
 * Called by DIR_$DO_OP case 0x4E.
 *
 * The returned UID has bit 1 (0x02) set in the high byte of the low word
 * for directory ACLs, or bit 2 (0x04) for file ACLs.
 *
 * Parameters:
 *   dir_uid    - UID of the directory
 *   acl_type   - ACL type UID (ACL_$DIR_ACL or ACL_$FILE_ACL)
 *   acl_ret    - Output: default ACL UID with type bits
 *   status_ret - Output: status code
 *
 * Original address: 0x00E53128
 * Original size: 160 bytes
 */

#include "dir/dir_internal.h"

void dir_$do_op_get_default_acl(uid_t *dir_uid, uid_t *acl_type, uid_t *acl_ret,
                                 status_$t *status_ret)
{
    uint32_t handle;
    uint32_t pad;   /* local_c[1] - unused but part of stack frame */

    ACL_$ENTER_SUPER();

    /* Open directory with read access (mode=1), no special rights */
    FUN_00e4ba02(dir_uid, 1, 0, &handle, status_ret);

    if (*status_ret == status_$ok) {
        *status_ret = status_$ok;

        if (acl_type->high == ACL_$DIR_ACL.high &&
            acl_type->low == ACL_$DIR_ACL.low) {
            /*
             * Directory ACL - read the ACL UID from the handle data
             * and set bit 1 in the high byte of the low word.
             *
             * The handle pointer points to the directory header page.
             * The ACL UID is at offset 0 from the handle data.
             */
            acl_ret->high = *(uint32_t *)handle;
            acl_ret->low = *(uint32_t *)(handle + 4);
            /* Set bit 1 in the MSB of low word (big-endian: byte at &low) */
            *((uint8_t *)&acl_ret->low) |= 0x02;
        }
        else if (acl_type->high == ACL_$FILE_ACL.high &&
                 acl_type->low == ACL_$FILE_ACL.low) {
            /*
             * File ACL - same structure, but set bit 2 instead.
             */
            acl_ret->high = *(uint32_t *)handle;
            acl_ret->low = *(uint32_t *)(handle + 4);
            /* Set bit 2 in the MSB of low word */
            *((uint8_t *)&acl_ret->low) |= 0x04;
        }
        else {
            /* Unknown ACL type */
            *status_ret = status_$naming_bad_type;
        }
    }

    /* Release directory handle */
    FUN_00e4b9d6(&handle);

    ACL_$EXIT_SUPER();
}
