/*
 * DIR_$DROPU - Drop a directory entry
 *
 * Removes an entry from a directory. This is a wrapper around
 * DIR_$DROP_HARD_LINKU that sets the output UID to NIL.
 *
 * Original address: 0x00E516C2
 * Original size: 58 bytes
 */

#include "dir/dir_internal.h"

/*
 * 0x00E50C5A, the word 0x0000 in the DIR code region (it sits after
 * dir_$add_bak_default_prot's neighbourhood, between the `rts` at
 * 0x00E50C58 and the `link` at 0x00E50C60).  Image bytes: 00 00.  It is
 * DIR_$DROP_HARD_LINKU's flags VAR argument - the callee copies the word
 * straight into the request at +0x90 (`move.w (A0),(-0x128,A6)` at
 * 0x00E51764).  Reached with `pea (-0xa7a,PC)` at 0x00E516D2, the only
 * reference to the cell.  (Ghidra labelled the cell by its address, 0x00E50C5A.)  source-ka0m.
 */
static const uint16_t dir_$dropu_link_flags_00e50c5a = 0;

/*
 * DIR_$DROPU - Drop a directory entry
 *
 * Removes an entry from a directory by calling DIR_$DROP_HARD_LINKU
 * with default flags, then sets the returned file_uid to UID_$NIL.
 *
 * Parameters:
 *   dir_uid    - UID of parent directory
 *   name       - Name of entry to drop
 *   name_len   - Pointer to name length
 *   file_uid   - Output: UID of dropped file (set to NIL)
 *   status_ret - Output: status code
 */
void DIR_$DROPU(uid_t *dir_uid, char *name, uint16_t *name_len,
                uid_t *file_uid, status_$t *status_ret)
{
    /* Drop the hard link with default flags */
    DIR_$DROP_HARD_LINKU(dir_uid, name, name_len,
                         (uint16_t *)&dir_$dropu_link_flags_00e50c5a,
                         status_ret);

    /* Set the file_uid output to NIL */
    file_uid->high = UID_$NIL.high;
    file_uid->low = UID_$NIL.low;
}
