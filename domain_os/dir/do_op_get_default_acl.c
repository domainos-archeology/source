/*
 * dir_$do_op_get_default_acl - server-side handler for GET_DEFAULT_ACL
 *
 * Original address: 0x00E53128
 * Original size: 160 bytes
 */

#include "dir/dir_internal.h"

/*
 * dir_$do_op_get_default_acl (0x00E53128)
 *
 * DIR_$DO_OP case 0x4E (0x00E4C7AC) calls it with
 * (&local_uid, request+0x8E, response+0x14, &response+0x04).
 *
 * Frame: `link.w A6,-0x8` - the whole frame is the handle cell at A6-0x8.
 *
 * Parameters (A6+0x08..A6+0x14):
 *   dir_uid    - UID of the directory
 *   acl_type   - requested ACL type uid (ACL_$DIR_ACL or ACL_$FILE_ACL)
 *   acl_ret    - out: the directory's default ACL uid, tagged
 *   status_ret - out: status code
 */
void dir_$do_op_get_default_acl(uid_t *dir_uid, uid_t *acl_type, uid_t *acl_ret,
                                status_$t *status_ret)
{
    uint32_t handle;        /* A6-0x8 */
    const uint32_t *hdr;

    /* 0x00E5313C */
    ACL_$ENTER_SUPER();

    /* 0x00E53142-0x00E53152: `move.l #0x10000` fills the mode / rights
     * word pair - mode 1, rights 0. */
    dir_$open_dir(dir_uid, 1, 0, &handle, status_ret);

    /* 0x00E5315A: A4 is loaded from the handle cell before the status is
     * tested, so it is read even on the failure path. */
    hdr = (const uint32_t *)ARCH_VA_TO_PTR(handle);

    /* 0x00E5315E: `tst.l (A3)` - the whole status longword. */
    if (*status_ret == status_$ok) {
        /* 0x00E53162: `clr.l (A3)` - redundant, the status is already 0. */
        *status_ret = status_$ok;

        /* 0x00E53164-0x00E53174: two `cmpm.l` steps compare the requested
         * type against the eight bytes at 0x00E1744C = ACL_$DIR_ACL.  The
         * `moveq #0x1,D0` at 0x00E5316C is dead. */
        if (acl_type->high == ACL_$DIR_ACL.high &&
            acl_type->low == ACL_$DIR_ACL.low) {
            /* 0x00E53176 */
            acl_ret->high = hdr[0];
            acl_ret->low = hdr[1];
            /* 0x00E5317E: `bset.b #0x1,(0x4,A2)` - the byte at acl_ret+4 is
             * the top byte of the low longword, so this is bit 25. */
            acl_ret->low |= 0x02000000u;
        }
        /* 0x00E53186-0x00E53196: the same against 0x00E17444 = ACL_$FILE_ACL. */
        else if (acl_type->high == ACL_$FILE_ACL.high &&
                 acl_type->low == ACL_$FILE_ACL.low) {
            /* 0x00E53198 */
            acl_ret->high = hdr[0];
            acl_ret->low = hdr[1];
            /* 0x00E531A0: `bset.b #0x2,(0x4,A2)` - bit 26. */
            acl_ret->low |= 0x04000000u;
        }
        else {
            /* 0x00E531A8 */
            *status_ret = status_$naming_bad_type;
        }
    }

    /* 0x00E531AE */
    dir_$release_handle(&handle);

    /* 0x00E531B8 */
    ACL_$EXIT_SUPER();
}
