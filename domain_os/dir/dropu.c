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

/* Reference to constant data at 0x00E50C5A - appears to be a flags value */
extern uint16_t DAT_00e50c5a;

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
    DIR_$DROP_HARD_LINKU(dir_uid, name, name_len, &DAT_00e50c5a, status_ret);

    /* Set the file_uid output to NIL */
    file_uid->high = UID_$NIL.high;
    file_uid->low = UID_$NIL.low;
}
