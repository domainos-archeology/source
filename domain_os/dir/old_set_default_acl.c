/*
 * DIR_$OLD_SET_DEFAULT_ACL - Legacy set default ACL
 *
 * Sets the default ACL for new entries in the directory.
 * Handles both local and remote directories.
 *
 * Original address: 0x00E561CC
 * Original size: 786 bytes
 */

#include "dir/dir_internal.h"

/* `move.w #0x8,-(SP)` at 0x00E5647E - the AST_$GET_COMMON_ATTRIBUTES
 * selector this site uses. */
#define DIR_CATTR_SET_DEF_ACL   0x0008

/*
 * DIR_$OLD_SET_DEFAULT_ACL - Legacy set default ACL
 *
 * Based on the Ghidra decompilation at 0x00E561CC:
 * 1. Check ACL_$RIGHTS on the directory
 * 2. Get location info for the directory
 * 3. If remote: delegate to REM_FILE_$SET_DEF_ACL
 * 4. If local:
 *    a. Read the info block
 *    b. If info block is too short, initialize default ACLs
 *    c. Set the appropriate ACL (dir or file) based on acl_type
 *    d. Write the info block back
 *    e. If old ACL existed, truncate/clean it up
 *
 * Parameters:
 *   dir_uid    - UID of directory
 *   acl_type   - ACL type UID (ACL_$DIR_ACL or ACL_$FILE_ACL)
 *   acl_uid    - ACL UID to set
 *   status_ret - Output: status code
 */
void DIR_$OLD_SET_DEFAULT_ACL(uid_t *dir_uid, uid_t *acl_type, uid_t *acl_uid,
                              status_$t *status_ret)
{
    uint32_t info_buf[4];     /* 16 bytes: dir_acl(8) + file_acl(8) */
    int16_t info_len;
    uid_t old_acl;            /* Previous ACL UID to clean up */
    uid_t location_uid;       /* local_6c/local_68 */
    /* A6-0x70 in the image, a full 0x20-byte object-location descriptor:
     * AST_$GET_ATTRIBUTES overwrites all 32 bytes on success (0x00E049B0),
     * so a 16-byte buffer here was overrunning the frame.
     * TODO(source-qgq): retype this as file_$obj_loc_t once the
     * AST_$GET_LOCATION output fields this function reads at +0x0C/+0x0D are
     * mapped onto its members. */
    uint8_t location_buf[0x20];
    uint8_t attr_byte;        /* local_58 */
    uint8_t attr_flags;       /* local_57 */
    uint8_t loc_buf1[4];      /* auStack_b0 */
    uint8_t loc_buf2[4];      /* auStack_ac */
    status_$t loc_status;     /* local_a8 */
    uid_t default_acl;
    uid_t *acl_to_set;
    ast_$common_attr_t common_attr;  /* A6-0x18, 0x18 bytes */
    char obj_type;            /* local_1b */
    uint16_t attr_val[2];     /* local_54 */

    /* Check ACL rights on the directory */
    ACL_$RIGHTS(dir_uid, &DAT_00e54b28, &DAT_00e564de,
                &ACL_TYPE_DIR, status_ret);
    if (*status_ret != status_$ok) {
        NAME_CONVERT_ACL_STATUS(status_ret);
        return;
    }

    /* Get location info */
    location_uid.high = ((uint32_t *)dir_uid)[0];
    location_uid.low = ((uint32_t *)dir_uid)[1];
    /* Clear bit 6 of attribute flags */
    /* attr_flags = attr_flags & 0xBF; (bclr #6) */
    AST_$GET_LOCATION(location_buf, 0, loc_buf1, loc_buf2, &loc_status);
    attr_byte = location_buf[0x0c]; /* local_58 */
    if (loc_status != status_$ok) {
        *status_ret = loc_status;
        return;
    }

    attr_flags = location_buf[0x0d]; /* local_57 */
    /* Check if remote */
    if ((int8_t)attr_flags < 0) {
        /* Remote directory - delegate to REM_FILE */
        REM_FILE_$SET_DEF_ACL(location_buf, dir_uid, acl_type, acl_uid, status_ret);
        if (*status_ret == file_$bad_reply_received_from_remote_node) {
            *status_ret = status_$naming_illegal_directory_operation;
        }
        return;
    }

    /* Local directory - read info block */
    DIR_$OLD_READ_INFOBLK(dir_uid, info_buf, &DAT_00e56096,
                          &info_len, status_ret);
    if (*status_ret != status_$ok) {
        return;
    }

    /* If info block is too short, initialize defaults */
    if (info_len < 0x10) {
        if (acl_type->high == ACL_$DIR_ACL.high &&
            acl_type->low == ACL_$DIR_ACL.low) {
            ACL_$DEFAULT_ACL(&default_acl, &NAME_$CONST_ZERO_W);
            info_buf[2] = default_acl.high; /* file ACL */
            info_buf[3] = default_acl.low;
        } else {
            ACL_$DEFAULT_ACL(&default_acl, &ACL_TYPE_DIR);
            info_buf[0] = default_acl.high; /* dir ACL */
            info_buf[1] = default_acl.low;
        }
        info_len = 0x10;
    }

    /* Set the appropriate ACL based on type */
    if (acl_type->high == ACL_$DIR_ACL.high &&
        acl_type->low == ACL_$DIR_ACL.low) {
        /* Setting dir ACL */
        old_acl.high = info_buf[0];
        old_acl.low = info_buf[1];

        acl_to_set = acl_uid;
        /* If NIL ACL, use default */
        if (acl_uid->high == ACL_$NIL.high &&
            acl_uid->low == ACL_$NIL.low) {
            acl_to_set = &ACL_$DNDCAL;
        }
        info_buf[0] = acl_to_set->high;
        info_buf[1] = acl_to_set->low;
    } else if (acl_type->high == ACL_$FILE_ACL.high &&
               acl_type->low == ACL_$FILE_ACL.low) {
        /* Setting file ACL */
        old_acl.high = info_buf[2];
        old_acl.low = info_buf[3];

        acl_to_set = acl_uid;
        /* If NIL ACL, use default */
        if (acl_uid->high == ACL_$NIL.high &&
            acl_uid->low == ACL_$NIL.low) {
            acl_to_set = &ACL_$FNDWRX;
        }
        info_buf[2] = acl_to_set->high;
        info_buf[3] = acl_to_set->low;
    } else {
        *status_ret = status_$naming_bad_type;
        return;
    }

    /* Check if new ACL is on same volume as directory */
    /* First (most significant) byte of the UID high word - tst.b on m68k */
    if ((char)(acl_uid->high >> 24) == '\0') {
        /* Same volume or null - just write info block */
        goto write_infoblk;
    }

    /* ACL is on different volume - need to verify and set attribute */
    location_uid.high = acl_uid->high;
    location_uid.low = acl_uid->low;
    AST_$GET_LOCATION(location_buf, 1, loc_buf1, loc_buf2, status_ret);
    if (*status_ret == status_$ok) {
        if (location_buf[0x0c] == attr_byte) {
            /* Same volume - set attribute */
            attr_val[0] = 1;
            AST_$SET_ATTRIBUTE(acl_uid, 6, attr_val, status_ret);
            if (*status_ret != status_$ok) {
                return;
            }
            goto write_infoblk;
        }
        /* Different volumes */
        *status_ret = file_$objects_on_different_volumes;
        return;
    }
    if (*status_ret != status_$wrong_type) {
        /* Error - set high bit */
        *status_ret |= 0x80000000;  /* or.b #0x80 into the first (MSB) byte on m68k */
        return;
    }
    *status_ret = file_$objects_on_different_volumes;
    return;

write_infoblk:
    /* Write the updated info block */
    DIR_$OLD_WRITE_INFOBLK(dir_uid, info_buf, &info_len, status_ret);
    if (*status_ret != status_$ok) {
        return;
    }

    /* Flush partial */
    FILE_$FW_PARTIAL(dir_uid, &DAT_00e54730, &DAT_00e564e2, status_ret);
    if (*status_ret != status_$ok) {
        /* Error - set high bit */
        *status_ret |= 0x80000000;  /* or.b #0x80 into the first (MSB) byte on m68k */
        return;
    }

    /* Check if old ACL needs cleanup */
    if ((int16_t)old_acl.high == 0) {
        return;
    }

    /* Old ACL exists - check if it's an ACL object and truncate */
    location_uid.high = old_acl.high;
    location_uid.low = old_acl.low;
    AST_$GET_COMMON_ATTRIBUTES((uid_t *)(void *)location_buf,
                               DIR_CATTR_SET_DEF_ACL, &common_attr,
                               &loc_status);            /* 0x00E56486 */
    if (loc_status == status_$ok) {
        /* 0x00E56498 `move.b (-0x17,A6),D1b` with the record at A6-0x18:
         * the object's sub-type.  3 is an ACL object. */
        if (common_attr.sub_type != 0x03) {
            /* Not an ACL object type */
            /* 0x00E564A2 `move.l #0xe002f,(A2)`. */
            *status_ret = status_$naming_object_is_not_an_acl_object;
            return;
        }
        /* Truncate the old ACL */
        AST_$TRUNCATE(&old_acl, 0, 3, (void *)loc_buf1, &loc_status);
        if (loc_status != status_$ok) {
            *status_ret = loc_status;
        }
    } else {
        *status_ret = loc_status;
    }
}
