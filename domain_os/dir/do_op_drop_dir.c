/*
 * dir_$do_op_drop_dir - DO_OP handler: drop/delete directory entry
 *
 * Removes a directory entry from its parent directory. Opens the parent
 * for write access with ACL right 3 (delete), looks up the entry by name,
 * and performs several validation checks:
 *
 * 1. Rejects link entries (type 4) - use drop_link instead
 * 2. Checks if the target UID is in the directory lock list (prevents
 *    deletion of directories that are currently open/locked)
 * 3. Verifies ACL rights on the target object
 * 4. If the target is a directory on the same volume, verifies it's
 *    empty (contains only 1 entry) before allowing deletion
 * 5. Truncates any ACL objects associated with the directory
 * 6. Deletes the directory object via FILE_$DELETE_OBJ
 * 7. Removes the name entry from the parent
 *
 * Falls back to DIR_$OLD_DROP_DIRU if status is naming_bad_directory.
 *
 * Called by DIR_$DO_OP case 0x3A. Audit code 0x17.
 *
 * Parameters:
 *   uid        - Parent directory UID
 *   name       - Entry name to drop
 *   name_len   - Length of name
 *   status_ret - Output: status code
 *
 * Original address: 0x00E52744
 * Original size: 682 bytes
 */

#include "dir/dir_internal.h"

/*
 * Constant cells for the ACL_$RIGHTS call at 0x00E52872, addressed with
 * `pea (d,PC)` (PC = instruction address + 2).
 */

/* 0x00E4BC24, byte 0xFF: ACL_$RIGHTS' ignore_super argument (TRUE - the
 * super-user bypass is suppressed).  `pea (-0x6c48,PC)` at 0x00E5286A. */

/* 0x00E51B64, longword 0x00000040: the required rights mask (delete).
 * `pea (-0xd04,PC)` at 0x00E52866. */
static const uint32_t dir_$drop_dir_rights_00e51b64 = 0x00000040;

/* 0x00E4B444, word 0x0001: ACL_$RIGHTS' option flags (object type 1,
 * directory).  `pea (-0x7420,PC)` at 0x00E52862. */

void dir_$do_op_drop_dir(uid_t *uid, void *name, uint16_t name_len,
                         status_$t *status_ret)
{
    char *blk = DIR_$BLOCK;   /* the routine's own A5 = 0x00E7DC00 */
    uint32_t parent_handle;
    uint32_t child_handle;
    void *parent_h;
    uid_t target_uid;
    uid_t acl_uid;
    char found;
    uint8_t *entry_ptr;
    void *entry_ret;
    uint8_t extra1[2];
    int16_t depth_ret;   /* dir_$find_entry's depth word (`clr.w (A0)` at 0x00E4C9F0) */
    status_$t local_status;

    /*
     * AST location descriptor struct (30+ bytes)
     * Layout relative to loc_desc base:
     *   +0x00: location result (2 bytes)
     *   +0x02: vol_id (2 bytes, int16_t)
     *   +0x08: uid.high (4 bytes, input)
     *   +0x0C: uid.low (4 bytes, input)
     *   +0x1D: flags byte (bit 6 cleared before call)
     */
    file_$obj_loc_t loc_desc;  /* 0x00e52824 pea (-0x28,A6); 0x20 bytes */
    uint32_t get_loc_buf1;   /* 0x00e5281c pea (-0x44,A6) - never touched */
    uint32_t get_loc_buf2;   /* 0x00e52818 pea (-0x48,A6) - aote+0x08 out */

    uint8_t delete_buf[8];
    uint8_t remove_buf[8];

    child_handle = 0;

    ACL_$ENTER_SUPER();

    /* Open parent directory with write access, ACL right 3 (delete) */
    dir_$open_dir(uid, 2, 3, &parent_handle, status_ret);
    parent_h = (void *)parent_handle;
    if (*status_ret != status_$ok) {
        goto done;
    }

    /* Look up the entry by name */
    found = dir_$find_entry(parent_h, name, name_len, 0,
                         &entry_ret, extra1, &depth_ret);
    entry_ptr = (uint8_t *)entry_ret;
    if (found >= 0) {
        /* Entry not found */
        *status_ret = status_$naming_name_not_found;
        goto done;
    }

    /* Reject link entries (type & 7 == 4) */
    if ((*entry_ptr & 7) == 4) {
        *status_ret = status_$naming_invalid_link_operation;
        goto done;
    }

    /* Copy target UID from entry (offset +4) */
    target_uid.high = *(uint32_t *)(entry_ptr + 4);
    target_uid.low = *(uint32_t *)(entry_ptr + 8);

    /*
     * A directory that is the source of a mount cannot be dropped.  The
     * cursor starts at A5+0x155C, which is mount table entry 1 (the tables
     * are ONE-BASED - see DIR_MOUNT_UID_TAB_OFF), and steps by 8.
     */
    {
        int16_t lock_count = DIR_MOUNT_COUNT16(blk);
        uint16_t i = lock_count - 1;
        if ((int16_t)i >= 0) {
            int32_t n = 1;
            do {
                if (target_uid.high == DIR_MOUNT_UID_OF(blk, n).high &&
                    target_uid.low  == DIR_MOUNT_UID_OF(blk, n).low) {
                    *status_ret = status_$naming_directory_locked;
                    goto done;
                }
                n++;
                i--;
            } while (i != 0xFFFF);
        }
    }

    /* Set up location descriptor with target UID */
    loc_desc.uid.high = target_uid.high;
    loc_desc.uid.low = target_uid.low;
    /* Clear bit 6 of flags byte in descriptor */
    loc_desc.flags &= (int8_t)~FILE_OBJ_LOC_SCRATCH;

    /* Get location info for the target */
    AST_$GET_LOCATION(&loc_desc, 1, &get_loc_buf1, &get_loc_buf2,
                      &local_status);

    /* Check if target is on same volume as parent and is local */
    /* The word at record+0x02 is the low half of the longword at +0x00. */
    if ((int16_t)loc_desc.volume ==
            *(int16_t *)((char *)parent_h + 0x3A) &&
        local_status == status_$ok &&
        loc_desc.flags >= 0) {

        /* Same volume, local object - check ACL rights */
        int16_t acl_result;
        acl_result = ACL_$RIGHTS(&target_uid,
                                 (boolean *)&DIR_$CONST_TRUE_B,
                                 (uint32_t *)&dir_$drop_dir_rights_00e51b64,
                                 (int16_t *)&DIR_$CONST_ONE_W,
                                 status_ret);

        if (acl_result == 0x40) {
            *status_ret = status_$naming_insufficient_rights;
            goto done;
        }

        {
            status_$t acl_status = *status_ret;
            if (acl_status != status_$ok &&
                acl_status != status_$no_right_to_perform_operation &&
                acl_status != status_$insufficient_rights_to_perform_operation) {
                goto done;
            }
        }

        /* Open the target directory to check if empty */
        dir_$open_dir(&target_uid, 2, 0, &child_handle, status_ret);
        if (*status_ret != status_$ok) {
            goto done;
        }

        /* Map page 0 of the target directory */
        {
            uint8_t *child_page;
            child_page = (uint8_t *)dir_$map_page((void *)child_handle, 0);

            /* Check entry type - must be 0 (normal/leaf page) */
            if ((*child_page >> 6) != 0) {
                *status_ret = status_$naming_directory_not_empty;
                goto done;
            }

            /* Calculate number of entries:
             * entry_count = (page[0x0E] - (page[0x14] + 0x12)) / 2
             * Directory is "empty" if it has exactly 1 entry (the "." entry) */
            {
                int32_t free_offset = (int32_t)*(int16_t *)(child_page + 0x0E);
                int32_t base_offset = (int32_t)(int16_t)(*(int16_t *)(child_page + 0x14) + 0x12);
                int32_t diff = free_offset - base_offset;
                if (diff < 0) {
                    diff += 1;
                }

                if ((diff >> 1) != 1) {
                    *status_ret = status_$naming_directory_not_empty;
                    goto done;
                }
            }

            /* Directory is empty - proceed with deletion */

            /* Truncate DIR ACL if present */
            if (child_page[0x46] != 0) {
                acl_uid.high = *(uint32_t *)(child_page + 0x46);
                acl_uid.low = *(uint32_t *)(child_page + 0x4A);
                AST_$TRUNCATE(&acl_uid, 0, 3, delete_buf, &local_status);
            }

            /* Truncate FILE ACL if present */
            if (child_page[0x7A] != 0) {
                acl_uid.high = *(uint32_t *)(child_page + 0x7A);
                acl_uid.low = *(uint32_t *)(child_page + 0x7E);
                AST_$TRUNCATE(&acl_uid, 0, 3, delete_buf, &local_status);
            }

            /* Unmap the child directory pages */
            DIR_$UNMAP_PAGES((void *)child_handle);

            /* Delete the directory object */
            FILE_$DELETE_OBJ(&target_uid, 0xFF, delete_buf, status_ret);
            if (*status_ret != status_$ok) {
                goto done;
            }
        }

        goto remove_entry;
    } else {
        /* Different volume or object not found - just remove the entry */
        *status_ret = local_status;
        if (*status_ret != status_$ok &&
            *status_ret != file_$object_not_found) {
            goto done;
        }

remove_entry:
        /* Remove the name entry from the parent directory */
        dir_$remove_entry(parent_h, name, name_len, 2, remove_buf, status_ret);
    }

done:
    dir_$release_handle(&parent_handle);
    dir_$release_handle(&child_handle);

    ACL_$EXIT_SUPER();

    /* 0x00E529CC-0x00E529DE: four longwords are pushed - the status
     * pointer, the ADDRESS of this routine's own name_len word parameter
     * (`pea (0x10,A6)`), the name and the uid. */
    if (*status_ret == status_$naming_bad_directory) {
        DIR_$OLD_DROP_DIRU(uid, name, &name_len, status_ret);
    }
}
