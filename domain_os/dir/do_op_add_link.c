/*
 * dir_$do_op_add_link - DO_OP handler for add entry/hard link
 *
 * Server-side handler for remote add (op 0x2A) and add hard link (op 0x2C)
 * operations dispatched by DIR_$DO_OP.
 *
 * Process:
 * 1. Call dir_$do_op_add_entry with dir_$find_entry as the name resolution
 *    callback. This adds the directory entry and fills the result buffer
 *    with entry metadata (entry type, target UID, flags, etc.).
 * 2. Copy file_uid into the result buffer and clear bit 6 of the flags byte.
 * 3. Call AST_$GET_COMMON_ATTRIBUTES (mask 0x90) to retrieve the target
 *    object's link count and type. The output overwrites part of the
 *    result buffer (offsets 0x08-0x23), which is no longer needed.
 * 4. If the object is not found and this is a regular add (not hard link),
 *    set bit 7 of flags and continue (skip entry validation).
 * 5. If status is OK and entry types match:
 *    a. Link count == 0: set attribute 6 (link count increment) to 1.
 *    b. Link count > 0: check ACL rights via ACL_$RIGHTS.
 *       - If (rights & 0x48) == 0x40 (modify but no link): insufficient rights.
 *       - If link count >= 0xFFF5: too many hard links.
 *       - Otherwise: set attribute 6 to 1.
 *    c. Other ACL status: convert via NAME_CONVERT_ACL_STATUS.
 * 6. On any failure: undo by calling dir_$do_op_drop_entry, then return error.
 *
 * Parameters:
 *   uid        - UID of the directory
 *   name       - Entry name
 *   name_len   - Length of name
 *   file_uid   - UID of file to link
 *   flags      - 0 for add, 0xFF (negative) for add hard link
 *   status_ret - Output: status code
 *
 * Original address: 0x00E5044A
 * Size: 378 bytes
 */

#include "dir/dir_internal.h"

/*
 * Constants for the ACL_$RIGHTS call.
 * In the original binary, these are inline data at 0x00E505C4-0x00E505C9
 * (embedded after the function's RTS instruction).
 */
static const uint32_t add_link_rights_mask = 0x00000048;  /* modify (0x40) + link (0x08) */
static const int16_t add_link_option_flags = -1;           /* all options */

/*
 * Offsets within the add_entry result buffer.
 *
 * The buffer is allocated as a single 0x4A-byte region matching the
 * original stack frame layout (A6-0x80 to A6-0x36). It serves dual
 * purpose: initially filled by dir_$do_op_add_entry with directory
 * entry metadata, then partially overwritten by AST_$GET_COMMON_ATTRIBUTES
 * output (offsets 0x08-0x23).
 *
 * Original stack layout (A6-relative):
 *   A6-0x80 = result[0x00]: entry_type from add_entry
 *   A6-0x78 = result[0x08]: AST common attributes output start
 *   A6-0x64 = result[0x1C]: link_count (from AST output)
 *   A6-0x60 = result[0x20]: target UID (from add_entry; AST input)
 *   A6-0x5E = result[0x22]: object type (from AST output)
 *   A6-0x58 = result[0x28]: file_uid copy
 *   A6-0x43 = result[0x3D]: flags byte (bit 6: cleared; bit 7: obj not found)
 *   A6-0x40 = result[0x40]: drop_entry cleanup buffer
 *   A6-0x38 = result[0x48]: attribute value for SET_ATTRIBUTE
 */
#define ADDRES_ENTRY_TYPE       0x00
#define ADDRES_COMMON_ATTRS     0x08
#define ADDRES_LINK_COUNT       0x1C
#define ADDRES_TARGET_UID       0x20
#define ADDRES_OBJ_TYPE         0x22
#define ADDRES_FILE_UID_COPY    0x28
#define ADDRES_FLAGS            0x3D
#define ADDRES_DROP_BUF         0x40
#define ADDRES_TOTAL_SIZE       0x48

/* Maximum hard link count before returning status_$naming_too_many_hard_links */
#define DIR_MAX_HARD_LINKS      0xFFF5

void dir_$do_op_add_link(uid_t *uid, void *name, uint16_t name_len,
                         uid_t *file_uid, uint16_t flags, status_$t *status_ret)
{
    uint8_t result[ADDRES_TOTAL_SIZE];
    status_$t status;

    /*
     * Step 1: Add directory entry.
     * Entry type 2 = file/link entry. dir_$find_entry is passed as the
     * name lookup callback for idempotent conflict resolution.
     */
    dir_$do_op_add_entry(uid, 2, name, name_len, 2, 0, file_uid, 0,
                         (uint32_t)(uintptr_t)dir_$find_entry,
                         result, &status);

    if (status != status_$ok) {
        *status_ret = status;
        return;
    }

    /*
     * Step 2: Copy file_uid into the result buffer and clear bit 6
     * of the flags byte. The flags byte was set by dir_$do_op_add_entry.
     */
    {
        uid_t *uid_copy = (uid_t *)(result + ADDRES_FILE_UID_COPY);
        uid_copy->high = file_uid->high;
        uid_copy->low = file_uid->low;
    }
    result[ADDRES_FLAGS] &= ~0x40;

    /*
     * Step 3: Get common attributes of the target object.
     * Mask 0x90 retrieves the link count (at result offset 0x1C) and
     * object type (at result offset 0x22). The target UID at offset 0x20
     * was filled by dir_$do_op_add_entry.
     *
     * The AST output overwrites result bytes 0x08-0x23, which is
     * intentional -- those add_entry result fields are no longer needed.
     */
    AST_$GET_COMMON_ATTRIBUTES((uid_t *)(result + ADDRES_TARGET_UID),
                               0x90,
                               result + ADDRES_COMMON_ATTRS,
                               &status);

    if (status != status_$ok) {
        /*
         * Step 4: For regular add (not hard link), if the target object
         * is not found, mark flags bit 7 and continue. This allows adding
         * directory entries that reference objects not yet visible locally.
         *
         * flags >= 0 means regular add (caller passed 0).
         * flags < 0 means hard link (caller passed 0xFF).
         */
        if ((int16_t)flags >= 0 && status == file_$object_not_found) {
            result[ADDRES_FLAGS] |= 0x80;
            status = status_$ok;
            goto check_entry;
        }
        goto check_status;
    }

check_entry:
    /*
     * Step 5: Validate the entry if:
     * - Flags bit 7 is NOT set (object was found)
     * - Status is OK
     * - Entry type from add_entry matches object type from attributes
     */
    if ((int8_t)result[ADDRES_FLAGS] < 0) {
        /* Object not found - skip validation */
        goto check_status;
    }
    if (status != status_$ok) {
        goto check_status;
    }
    if (*(int16_t *)(result + ADDRES_ENTRY_TYPE) !=
        *(int16_t *)(result + ADDRES_OBJ_TYPE)) {
        /* Entry type mismatch - skip (status remains OK) */
        goto check_status;
    }

    {
        uint16_t link_count = *(uint16_t *)(result + ADDRES_LINK_COUNT);

        if (link_count == 0) {
            /* First link: no rights check needed */
            goto set_attr;
        }

        /*
         * Step 6: Check ACL rights before adding another hard link.
         * Rights mask 0x48 checks for modify (0x40) and link (0x08) rights.
         */
        {
            int16_t rights = ACL_$RIGHTS(
                file_uid,
                (void *)&add_link_rights_mask,  /* unused param; valid pointer */
                (uint32_t *)&add_link_rights_mask,
                (int16_t *)&add_link_option_flags,
                &status);

            if (status == status_$insufficient_rights_to_perform_operation ||
                status == status_$ok ||
                status == status_$no_right_to_perform_operation) {
                /*
                 * Check the returned rights bits:
                 * - If (rights & 0x48) == 0x40: has modify but not link right
                 *   → naming_insufficient_rights
                 * - Otherwise: rights are sufficient, check link count limit
                 */
                if ((rights & 0x48) == 0x40) {
                    status = status_$naming_insufficient_rights;
                } else if (link_count >= DIR_MAX_HARD_LINKS) {
                    status = status_$naming_too_many_hard_links;
                } else {
                    goto set_attr;
                }
            } else {
                /* Other ACL error: convert to naming status */
                NAME_CONVERT_ACL_STATUS(&status);
            }
        }
    }
    goto check_status;

set_attr:
    /*
     * Step 7: Increment the hard link count by setting attribute 6 to 1.
     * Attribute 6 is the link count increment attribute.
     */
    {
        uint16_t attr_value = 1;
        AST_$SET_ATTRIBUTE(file_uid, 6, &attr_value, &status);
    }

check_status:
    if (status != status_$ok) {
        *status_ret = status;
        /*
         * Step 8: Undo the add_entry by dropping the entry.
         * Rights = 0 (no ACL check for cleanup), entry_type = 2.
         */
        dir_$do_op_drop_entry(uid, 0, name, name_len, 2,
                              result + ADDRES_DROP_BUF, &status);
        return;
    }
    *status_ret = status;
}
