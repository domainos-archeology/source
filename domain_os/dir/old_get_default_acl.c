/*
 * DIR_$OLD_GET_DEFAULT_ACL - Legacy get default ACL
 *
 * Reads the directory info block and extracts the default ACL
 * for the requested type.
 *
 * Original address: 0x00E564E6
 * Original size: 210 bytes
 */

#include "dir/dir_internal.h"

/*
 * DIR_$OLD_GET_DEFAULT_ACL - Legacy get default ACL
 *
 * Reads the info block via DIR_$OLD_READ_INFOBLK. If the info block
 * is too short (< 0x10 bytes), falls back to ACL_$DEFAULT_ACL.
 * Checks the acl_type against ACL_$DIR_ACL and ACL_$FILE_ACL
 * to determine which ACL UID to return from the info block.
 *
 * Parameters:
 *   dir_uid    - UID of directory
 *   acl_type   - ACL type UID (ACL_$DIR_ACL or ACL_$FILE_ACL)
 *   acl_ret    - Output: default ACL UID
 *   status_ret - Output: status code
 */
void DIR_$OLD_GET_DEFAULT_ACL(uid_t *dir_uid, uid_t *acl_type, uid_t *acl_ret,
                              status_$t *status_ret)
{
    /* Info block layout:
     * offset 0x00-0x07: dir ACL UID (high, low)
     * offset 0x08-0x0F: file ACL UID (high, low)
     */
    uint32_t info_buf[4];  /* 16 bytes for 2 UIDs */
    int16_t info_len;
    uid_t default_acl;

    /* Read the info block */
    DIR_$OLD_READ_INFOBLK(dir_uid, info_buf, &DAT_00e56096,
                          &info_len, status_ret);
    if ((int16_t)*status_ret != 0) {
        /* If read fails, use system default */
        if (acl_type->high == ACL_$DIR_ACL.high &&
            acl_type->low == ACL_$DIR_ACL.low) {
            ACL_$DEFAULT_ACL(&default_acl, &ACL_TYPE_FILE);
        } else {
            ACL_$DEFAULT_ACL(&default_acl, &ACL_TYPE_DIR);
        }
        acl_ret->high = default_acl.high;
        acl_ret->low = default_acl.low;
        *status_ret = status_$ok;
        return;
    }

    /* If info block is too short, use defaults */
    if (info_len < 0x10) {
        if (acl_type->high == ACL_$DIR_ACL.high &&
            acl_type->low == ACL_$DIR_ACL.low) {
            ACL_$DEFAULT_ACL(&default_acl, &ACL_TYPE_FILE);
            /* Also set a default for file ACL side */
        } else {
            ACL_$DEFAULT_ACL(&default_acl, &ACL_TYPE_DIR);
        }
        acl_ret->high = default_acl.high;
        acl_ret->low = default_acl.low;
        *status_ret = status_$ok;
        return;
    }

    /* Extract ACL from info block based on type */
    if (acl_type->high == ACL_$DIR_ACL.high &&
        acl_type->low == ACL_$DIR_ACL.low) {
        /* Return the dir ACL (first UID in info block) */
        acl_ret->high = info_buf[0];
        acl_ret->low = info_buf[1];
    } else if (acl_type->high == ACL_$FILE_ACL.high &&
               acl_type->low == ACL_$FILE_ACL.low) {
        /* Return the file ACL (second UID in info block) */
        acl_ret->high = info_buf[2];
        acl_ret->low = info_buf[3];
    } else {
        *status_ret = status_$naming_bad_type;
        return;
    }

    *status_ret = status_$ok;
}
