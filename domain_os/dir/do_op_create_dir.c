/*
 * dir_$do_op_create_dir - DO_OP handler: create subdirectory
 *
 * Creates a new subdirectory within a parent directory. Opens the parent
 * for write access with ACL right 2 (create), reads its default ACL UIDs
 * from page 0 (offset 0x46 for DIR ACL, 0x7A for FILE ACL), calls
 * dir_$create_dir_obj to create and initialize the new directory, then adds
 * the entry via dir_$add_entry.
 *
 * On failure to add the entry (name collision), cleans up the created
 * directory by resetting ACL attributes and setting refcount to zero.
 * For server processes (type 9), handles idempotent create: if name
 * already exists, looks up the existing entry and returns its UID if
 * the existing object is a directory (type 1 or 2).
 *
 * Called by DIR_$DO_OP case 0x38. Audit code 0x16.
 *
 * Parameters:
 *   uid        - Parent directory UID
 *   name       - Entry name to create
 *   name_len   - Length of name
 *   result_uid - Output: UID of created (or found) directory
 *   status_ret - Output: status code
 *
 * Original address: 0x00E52576
 * Original size: 462 bytes
 */

#include "dir/dir_internal.h"

/* `move.w #0x88,-(SP)` at 0x00E526FE - the AST_$GET_COMMON_ATTRIBUTES
 * selector this site uses. */
#define DIR_CATTR_CREATE_DIR    0x0088

void dir_$do_op_create_dir(uid_t *uid, void *name, uint16_t name_len,
                           void *result_uid, status_$t *status_ret)
{
    uid_t *new_uid = (uid_t *)result_uid;
    uint32_t local_handle;
    status_$t status;
    uid_t dir_acl_uid;
    uid_t file_acl_uid;
    uint8_t *page_data;
    uint16_t attr_val;

    ACL_$ENTER_SUPER();

    /* Open parent directory with write access, ACL right 2 (create) */
    dir_$open_dir(uid, 2, 2, &local_handle, status_ret);
    if (*status_ret != status_$ok) {
        goto cleanup;
    }

    /* Map page 0 to read default ACLs */
    page_data = (uint8_t *)dir_$map_page((void *)local_handle, 0);

    /* Copy DIR default ACL UID from page offset 0x46 */
    dir_acl_uid.high = *(uint32_t *)(page_data + 0x46);
    dir_acl_uid.low  = *(uint32_t *)(page_data + 0x4A);

    /* Copy FILE default ACL UID from page offset 0x7A */
    file_acl_uid.high = *(uint32_t *)(page_data + 0x7A);
    file_acl_uid.low  = *(uint32_t *)(page_data + 0x7E);

    /* Create new directory object */
    dir_$create_dir_obj(uid, page_data, &dir_acl_uid, &file_acl_uid,
                        new_uid, status_ret);
    if (*status_ret != status_$ok) {
        goto cleanup;
    }

    /* Add entry to parent directory */
    dir_$add_entry(local_handle, name, name_len, 2, 0, new_uid,
                   0, dir_$find_entry, status_ret);
    if (*status_ret != status_$ok) {
        /* Add failed - clean up the created directory */

        /* Reset ACL attributes (attribute 7 = refcount, set to 1) */
        attr_val = 1;
        if ((uint8_t)(dir_acl_uid.high >> 24) != 0) {
            AST_$SET_ATTRIBUTE(&dir_acl_uid, 7, &attr_val, &status);
        }
        if ((uint8_t)(file_acl_uid.high >> 24) != 0) {
            AST_$SET_ATTRIBUTE(&file_acl_uid, 7, &attr_val, &status);
        }

        /* Set refcount to zero to mark for deletion */
        FILE_$SET_REFCNT(new_uid, &DIR_$CONST_ZERO_L, &status);

        /* Clear the result UID */
        new_uid->high = UID_$NIL.high;
        new_uid->low = UID_$NIL.low;

        /* Idempotent handling for server processes (type 9) */
        if (*status_ret == status_$name_already_exists) {
            if ((int16_t)PROC1_$DATA.type[(int16_t)PROC1_$CURRENT] == 9) {
                /* Look up the existing entry */
                void *entry_ptr;
                uint8_t extra1[2];
                int16_t depth_ret;  /* `clr.w (A0)` at 0x00E4C9F0 */
                char found;

                found = dir_$find_entry((void *)local_handle, name,
                                     name_len, 0, &entry_ptr,
                                     extra1, &depth_ret);
                if (found < 0) {
                    /* Entry found - copy its UID */
                    uint8_t *ep = (uint8_t *)entry_ptr;
                    new_uid->high = *(uint32_t *)(ep + 4);
                    new_uid->low = *(uint32_t *)(ep + 8);

                    /* Verify it's a directory by checking common attributes */
                    file_$obj_loc_t    desc;    /* A6-0x58, 0x20 bytes */
                    ast_$common_attr_t cattr;   /* A6-0x70, 0x18 bytes */

                    /* 0x00E526E6: the UID goes to descriptor+0x08. */
                    desc.uid = *new_uid;
                    /* 0x00E526EE `bclr.b #0x6,(-0x3b,A6)` = descriptor+0x1D. */
                    desc.flags &= (int8_t)~FILE_OBJ_LOC_SCRATCH;

                    AST_$GET_COMMON_ATTRIBUTES(&desc,
                                               DIR_CATTR_CREATE_DIR,
                                               &cattr, &status);  /* 0x00E52706 */
                    if (status == status_$ok) {
                        /* 0x00E52718 `move.b (-0x6f,A6),D0b` with the record
                         * at A6-0x70: the object's sub-type. */
                        if (cattr.sub_type == 1 || cattr.sub_type == 2) {
                            *status_ret = status_$ok;   /* 0x00E52728 clr.l (A3) */
                        }
                    }
                }
            }
        }
    }

cleanup:
    dir_$release_handle(&local_handle);
    ACL_$EXIT_SUPER();
}
