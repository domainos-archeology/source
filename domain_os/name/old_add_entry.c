/*
 * name_$old_add_entry - Name-level add directory entry
 *
 * Validates the leaf name, locks the directory, adds the entry via
 * an internal helper, updates the hint table, and unlocks.
 *
 * Process:
 * 1. Validate leaf name via name_$validate_leaf
 *    - On failure: return status_$naming_invalid_leaf
 * 2. Lock directory via NAME_$LOCK_DIR (with flags from type param)
 * 3. Call dir_$old_add_entry_ext to add entry to directory buffer
 *    (with UID, type, location data, replace flag 0xFF)
 * 4. On success:
 *    - Extract location info from target UID
 *    - Update hint table via HINT_$ADDI
 * 5. Unlock directory via NAME_$UNLOCK_DIR
 *    - Propagate unlock errors
 * 6. Exit super mode via ACL_$EXIT_SUPER
 *
 * Parameters:
 *   dir_uid    - UID of directory
 *   type       - Entry type code (e.g., 2 for root entries)
 *   name       - Entry name
 *   name_len   - Length of name
 *   file_uid   - UID of file to add
 *   flags      - Operation flags (location info)
 *   status_ret - Output: status code
 *
 * Original address: 0x00E56682
 * Size: 202 bytes
 */

#include "name/name_internal.h"
#include "dir/dir.h"
#include "hint/hint.h"       /* HINT_$ADDI */

void name_$old_add_entry(uid_t *dir_uid, uint16_t type, char *name,
                         uint16_t name_len, uid_t *file_uid,
                         uint32_t flags, status_$t *status_ret)
{
    int8_t valid;                   /* D0 from name_$validate_leaf */
    uint16_t parsed_len;            /* A6-0x3E */
    uint32_t handle;                /* A6-0x3C */
    status_$t unlock_status;        /* A6-0x38 */
    uint8_t parsed_name[32];        /* A6-0x30 */
    /*
     * 0x00E5670A-0x00E56718 fills A6-0x10 and A6-0x0C, then hands HINT_$ADDI
     * `pea (-0x10,A6)` - ONE 8-byte record, not two independent locals whose
     * adjacency the C standard does not promise.  It is a hint_addr_t: the
     * first longword is the caller's location word and the second the node
     * the file lives on.  (source-0o3n)
     */
    hint_addr_t location;           /* A6-0x10 */
    uint8_t result[4];              /* A6-0x34 */

    valid = name_$validate_leaf(name, name_len, parsed_name, &parsed_len);
    if (valid < 0) {
        /* Valid leaf name */
        NAME_$LOCK_DIR(dir_uid, &handle, 4, (int16_t)type, status_ret);
        if (*status_ret == status_$ok) {
            dir_$old_add_entry_ext(dir_uid, handle, parsed_name, parsed_len,
                         1, file_uid, flags, 0xFF, result, status_ret);
            if (*status_ret == status_$ok) {
                /* 0x00E5670A `move.l D2,(-0x10,A6)`: D2 is the A6+0x18
                 * parameter, the caller's location word. */
                location.flags = flags;
                /* 0x00E5670E-0x00E56718 `move.l #0xfffff,D0` /
                 * `and.l (0x4,A4),D0` / `move.l D0,(-0xc,A6)`: the low 20
                 * bits of the file UID are the node id. */
                location.node_id = file_uid->low & 0xFFFFF;
                /* 0x00E5671C-0x00E56722: both arguments by address. */
                HINT_$ADDI(file_uid, (uint32_t *)&location);
            }
            NAME_$UNLOCK_DIR(&unlock_status);
            if (unlock_status != status_$ok) {
                *status_ret = unlock_status;
            }
        }
        ACL_$EXIT_SUPER();
    } else {
        *status_ret = status_$naming_invalid_leaf;
    }
}
