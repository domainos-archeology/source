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
 * Constant cells for the ACL_$RIGHTS call at 0x00E561FA, addressed with
 * `pea (d,PC)` (PC = instruction address + 2).
 */

/* 0x00E54B28, byte 0x00: ACL_$RIGHTS' ignore_super argument (FALSE - the
 * super-user bypass applies).  `pea (-0x16ce,PC)` at 0x00E561F4. */
static const boolean dir_$set_def_acl_ignore_super_00e54b28 = false;

/* 0x00E564DE, longword 0x00000008: the required rights mask.
 * `pea (0x2ec,PC)` at 0x00E561F0. */
static const uint32_t dir_$set_def_acl_rights_00e564de = 0x00000008;

/* 0x00E54B26, word 0x0001: ACL_$RIGHTS' option flags (object type 1,
 * directory).  `pea (-0x16c8,PC)` at 0x00E561EC. */
static const int16_t dir_$set_def_acl_acl_opts_00e54b26 = 1;

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
    /* A6-0x70 in the image: the 0x20-byte object-location descriptor.
     * The UID goes in at +0x08 (A6-0x68) and the flags byte is +0x1D
     * (A6-0x53); the byte the volume comparison uses is +0x1C (A6-0x54). */
    file_$obj_loc_t location_buf;
    uint8_t attr_byte;        /* D2.B: location_buf.rights_bits (+0x1C) */
    uint32_t loc_buf1;        /* A6-0xac; pea'd but never touched by callee */
    uint32_t loc_buf2;        /* A6-0xa8; receives aote+0x08 */
    uint8_t trunc_result;     /* A6-0xb2; AST_$TRUNCATE result byte (0x00e564b0) */
    status_$t loc_status;     /* local_a8 */
    uid_t default_acl;
    uid_t *acl_to_set;
    ast_$common_attr_t common_attr;  /* A6-0x18, 0x18 bytes */
    char obj_type;            /* local_1b */
    uint16_t attr_val[2];     /* local_54 */

    /* Check ACL rights on the directory */
    ACL_$RIGHTS(dir_uid,
                (boolean *)&dir_$set_def_acl_ignore_super_00e54b28,
                (uint32_t *)&dir_$set_def_acl_rights_00e564de,
                (int16_t *)&dir_$set_def_acl_acl_opts_00e54b26, status_ret);
    if (*status_ret != status_$ok) {
        NAME_CONVERT_ACL_STATUS(status_ret);
        return;
    }

    /* Get location info */
    location_buf.uid.high = ((uint32_t *)dir_uid)[0];
    location_buf.uid.low = ((uint32_t *)dir_uid)[1];
    /* 0x00E5621E `bclr.b #0x6,(-0x53,A6)` = the record's flags byte at +0x1D */
    location_buf.flags &= (int8_t)~FILE_OBJ_LOC_SCRATCH;
    AST_$GET_LOCATION(&location_buf, 0, &loc_buf1, &loc_buf2, &loc_status);
    /* 0x00E56250 `move.b (-0x54,A6),D2b` = the byte at record+0x1C */
    attr_byte = (uint8_t)location_buf.rights_bits;
    if (loc_status != status_$ok) {
        *status_ret = loc_status;
        return;
    }

    /* 0x00E56254 `tst.b (-0x53,A6)` / bpl: the record's flags byte */
    if (location_buf.flags < 0) {
        /* Remote directory - delegate to REM_FILE (0x00E56262 pea's
         * A6-0x60, i.e. the record base + 0x10). */
        REM_FILE_$SET_DEF_ACL(&location_buf.loc_info, dir_uid, acl_type,
                              acl_uid, status_ret);
        if (*status_ret == file_$bad_reply_received_from_remote_node) {
            *status_ret = status_$naming_illegal_directory_operation;
        }
        return;
    }

    /* Local directory - read info block */
    DIR_$OLD_READ_INFOBLK(dir_uid, info_buf, &DIR_$INFOBLK_MAX_LEN,
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
    location_buf.uid.high = acl_uid->high;
    location_buf.uid.low = acl_uid->low;
    location_buf.flags &= (int8_t)~FILE_OBJ_LOC_SCRATCH;   /* 0x00E563BA */
    AST_$GET_LOCATION(&location_buf, 1, &loc_buf1, &loc_buf2, status_ret);
    if (*status_ret == status_$ok) {
        /* 0x00E563F0: the same +0x1C byte compared against D2 */
        if ((uint8_t)location_buf.rights_bits == attr_byte) {
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
    if (*status_ret != status_$file_object_not_found) {
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
    FILE_$FW_PARTIAL(dir_uid, &NAME_$CONST_ZERO_L, &DAT_00e564e2, status_ret);
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
    location_buf.uid.high = old_acl.high;
    location_buf.uid.low = old_acl.low;
    AST_$GET_COMMON_ATTRIBUTES(&location_buf,
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
        /* 0x00e564b0 pea (-0xb2,A6): a result cell of its own, not loc_buf1. */
        AST_$TRUNCATE(&old_acl, 0, 3, &trunc_result, &loc_status);
        if (loc_status != status_$ok) {
            *status_ret = loc_status;
        }
    } else {
        *status_ret = loc_status;
    }
}
