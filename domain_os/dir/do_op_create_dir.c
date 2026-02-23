/*
 * dir_$do_op_create_dir - DO_OP handler: create subdirectory
 *
 * Creates a new subdirectory within a parent directory. Opens the parent
 * for write access with ACL right 2 (create), reads its default ACL UIDs
 * from page 0 (offset 0x46 for DIR ACL, 0x7A for FILE ACL), calls
 * FUN_00e52394 to create and initialize the new directory, then adds
 * the entry via FUN_00e4fe0a.
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
    FUN_00e52394(uid, page_data, &dir_acl_uid, &file_acl_uid,
                 new_uid, status_ret);
    if (*status_ret != status_$ok) {
        goto cleanup;
    }

    /* Add entry to parent directory */
    FUN_00e4fe0a(local_handle, name, name_len, 2, 0, new_uid,
                 0, FUN_00e4c9e4, status_ret);
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
        FILE_$SET_REFCNT(new_uid, &DAT_00e4b33c, &status);

        /* Clear the result UID */
        new_uid->high = UID_$NIL.high;
        new_uid->low = UID_$NIL.low;

        /* Idempotent handling for server processes (type 9) */
        if (*status_ret == status_$name_already_exists) {
            if (*(int16_t *)((char *)PROC1_$TYPE +
                (int16_t)(PROC1_$CURRENT * 2)) == 9) {
                /* Look up the existing entry */
                void *entry_ptr;
                uint8_t extra1[2];
                uint8_t extra2[2];
                char found;

                found = FUN_00e4c9e4((void *)local_handle, name,
                                     name_len, 0, &entry_ptr,
                                     extra1, extra2);
                if (found < 0) {
                    /* Entry found - copy its UID */
                    uint8_t *ep = (uint8_t *)entry_ptr;
                    new_uid->high = *(uint32_t *)(ep + 4);
                    new_uid->low = *(uint32_t *)(ep + 8);

                    /* Verify it's a directory by checking common attributes */
                    uid_t check_uid;
                    uint8_t attr_buf[8];
                    uint8_t common_buf[40];
                    uint8_t type_byte;

                    check_uid.high = new_uid->high;
                    check_uid.low = new_uid->low;

                    /* Clear bit 6 of the flags byte at offset within attr_buf */
                    /* (This corresponds to bclr #6,(-0x3b,A6) in assembly) */

                    AST_$GET_COMMON_ATTRIBUTES(common_buf, 0x88,
                                               attr_buf, &status);
                    if (status == status_$ok) {
                        type_byte = attr_buf[4];  /* type field */
                        if (type_byte == 1 || type_byte == 2) {
                            *status_ret = status_$ok;
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
