/*
 * DIR_$OLD_GET_DEFAULT_ACL - legacy read of a directory's default ACL
 *
 * Original address: 0x00E564E6
 * Original size: 210 bytes
 */

#include "dir/dir_internal.h"

/*
 * DIR_$OLD_GET_DEFAULT_ACL (0x00E564E6)
 *
 * Reads the directory's info block with DIR_$OLD_READ_INFOBLK (0x00E560A6)
 * and hands back the ACL uid the requested type names.  When the directory
 * has no info block to speak of it produces the system default instead.
 *
 * Frame: `link.w A6,-0x34`, A5 = 0xE7FD24 (the shared NAME/DIR block).
 *   A6-0x34  the uid ACL_$DEFAULT_ACL fills in
 *   A6-0x2A  the info-block length DIR_$OLD_READ_INFOBLK reports
 *   A6-0x28  the 0x28-byte info block: the DIR ACL uid at +0x00, the FILE
 *            ACL uid at +0x08
 *
 * Parameters (A6+0x08..A6+0x14):
 *   dir_uid    - UID of the directory
 *   acl_type   - ACL_$DIR_ACL or ACL_$FILE_ACL
 *   acl_ret    - out: the ACL uid
 *   status_ret - out: status code
 */
void DIR_$OLD_GET_DEFAULT_ACL(uid_t *dir_uid, uid_t *acl_type, uid_t *acl_ret,
                              status_$t *status_ret)
{
    uint8_t info_buf[0x28];     /* A6-0x28 */
    int16_t info_len;           /* A6-0x2A */
    uid_t   default_acl;        /* A6-0x34 */
    const uint32_t *src;

    /* 0x00E56500-0x00E56512 */
    DIR_$OLD_READ_INFOBLK(dir_uid, info_buf, &DIR_$INFOBLK_MAX_LEN,
                          &info_len, status_ret);

    /* 0x00E5651A-0x00E5652A: the default path is taken ONLY for
     * "illegal directory operation" or an info block shorter than 0x10;
     * every other status falls through to the normal path, which returns
     * without touching acl_ret when the status' LOW WORD is non-zero. */
    if (*status_ret == status_$naming_illegal_directory_operation ||
        info_len < 0x10) {
        /* 0x00E5652C: `clr.l (A4)` */
        *status_ret = status_$ok;

        if (acl_type->high == ACL_$DIR_ACL.high &&
            acl_type->low == ACL_$DIR_ACL.low) {
            /* 0x00E56540 `pea (-0x1a1c,PC)` = 0x00E54B26, the word 1. */
            ACL_$DEFAULT_ACL(&default_acl, &ACL_TYPE_DIR);
        } else if (acl_type->high == ACL_$FILE_ACL.high &&
                   acl_type->low == ACL_$FILE_ACL.low) {
            /* 0x00E56558 `pea (-0x1e2c,PC)` = 0x00E5472E, the word 0. */
            ACL_$DEFAULT_ACL(&default_acl, &NAME_$CONST_ZERO_W);
        } else {
            /* 0x00E565A8: an unknown type never reaches ACL_$DEFAULT_ACL. */
            *status_ret = status_$naming_bad_type;
            return;
        }

        /* 0x00E56566 / 0x00E565A0 */
        acl_ret->high = default_acl.high;
        acl_ret->low = default_acl.low;
        return;
    }

    /* 0x00E5656C: `tst.w (0x2,A4)` - the status' LOW WORD only. */
    if ((int16_t)*status_ret != 0) {
        return;
    }

    /* 0x00E56572-0x00E5659C */
    if (acl_type->high == ACL_$DIR_ACL.high &&
        acl_type->low == ACL_$DIR_ACL.low) {
        src = (const uint32_t *)(const void *)&info_buf[0x00];
    } else if (acl_type->high == ACL_$FILE_ACL.high &&
               acl_type->low == ACL_$FILE_ACL.low) {
        src = (const uint32_t *)(const void *)&info_buf[0x08];
    } else {
        /* 0x00E565A8 */
        *status_ret = status_$naming_bad_type;
        return;
    }

    /* 0x00E565A0: the status is NOT cleared on this path. */
    acl_ret->high = src[0];
    acl_ret->low = src[1];
}
