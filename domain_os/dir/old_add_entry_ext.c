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
 *   replace_flag - Domain boolean byte at A6+0x20; forwarded to
 *                  dir_$old_add_entry with `move.b (0x20,A6),-(SP)`
 *                  (0x00E5541E), which lands in the EVEN (high) byte of
 *                  the callee's 2-byte slot at its A6+0x1C (source-j8qj)
 *   result       - Output: pointer to new entry (indirect)
 *   status_ret   - Output: status code
 *
 * Original address: 0x00E55406
 * Size: 86 bytes
 */

#include "dir/dir_internal.h"

void dir_$old_add_entry_ext(uid_t *dir_uid, uint32_t handle, uint8_t *name,
                            uint16_t name_len, uint16_t type, void *uid_data,
                            uint32_t extra, boolean replace_flag,
                            uint8_t *result, status_$t *status_ret)
{
    /* 0x00E55416-0x00E5543A: the eight arguments are re-pushed unchanged,
     * with replace_flag pushed as a BYTE. */
    dir_$old_add_entry(dir_uid, handle, name, name_len, type, uid_data,
                       replace_flag, result, status_ret);

    /* 0x00E55442: `tst.w (0x2,A2)` - only the LOW word of the status is
     * tested, i.e. the status subsystem/module halves are ignored. */
    if ((int16_t)*status_ret == 0) {
        /* Store extra value at offset 0x20 of the new entry.
         * result is an indirect pointer: *result points to the entry */
        void *entry = *(void **)result;
        *((uint32_t *)((char *)entry + 0x20)) = extra;
    }
}
