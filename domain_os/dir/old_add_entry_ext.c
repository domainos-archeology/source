/*
 * dir_$old_add_entry_ext - Add entry to directory with extra field
 *
 * Thin wrapper around dir_$old_add_entry. After successful add,
 * stores the 'extra' value at offset 0x20 of the new directory
 * entry structure. Used for root directory entries and entries
 * needing location/generation info stored in the entry.
 *
 * Parameters:
 *   dir_uid      - UID of directory
 *   handle       - Mapped directory buffer handle
 *   name         - Entry name
 *   name_len     - Length of name
 *   type         - Entry type code
 *   uid_data     - UID data for entry
 *   extra        - Extra value to store at entry offset 0x20
 *   replace_flag - Replace flag (passed through)
 *   result       - Output: pointer to new entry (indirect)
 *   status_ret   - Output: status code
 *
 * Original address: 0x00E55406
 * Size: 86 bytes
 */

#include "dir/dir_internal.h"

void dir_$old_add_entry_ext(uid_t *dir_uid, uint32_t handle, uint8_t *name,
                            uint16_t name_len, uint16_t type, void *uid_data,
                            uint32_t extra, uint8_t replace_flag,
                            uint8_t *result, status_$t *status_ret)
{
    /* Call the base add entry function, passing replace_flag in
     * the flags position (CONCAT11 in assembly merges bytes) */
    dir_$old_add_entry(dir_uid, handle, name, name_len, type, uid_data,
                       (uint16_t)replace_flag, result, status_ret);

    if ((int16_t)*status_ret == 0) {
        /* Store extra value at offset 0x20 of the new entry.
         * result is an indirect pointer: *result points to the entry */
        void *entry = *(void **)result;
        *((uint32_t *)((char *)entry + 0x20)) = extra;
    }
}
