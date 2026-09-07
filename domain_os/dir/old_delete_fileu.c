/*
 * DIR_$OLD_DELETE_FILEU - Legacy delete file from directory
 *
 * Thin wrapper around NAME_$OLD_DELETE_ENTRYU for file deletion.
 *
 * Original address: 0x00E5716E
 * Original size: 64 bytes
 */

#include "dir/dir_internal.h"

/*
 * DIR_$OLD_DELETE_FILEU - Legacy delete file from directory
 *
 * Calls the shared delete/drop helper with the appropriate flags
 * extracted from param5 and param4.
 *
 * Parameters:
 *   dir_uid    - UID of parent directory
 *   name       - Name of entry to delete
 *   name_len   - Pointer to name length
 *   status_ret       - Output: status code (A6+0x14, pushed first at
 *                      0x00E5717C so it is the callee's LAST argument)
 *   check_del_right  - POINTER to a Domain boolean (A6+0x18);
 *                      `movea.l (0x18,A6),A1 / move.b (A1),-(SP)` at
 *                      0x00E5718C supplies NAME_$OLD_DELETE_ENTRYU's arg 4
 *   no_lock          - POINTER to a Domain boolean (A6+0x1C);
 *                      `movea.l (0x1c,A6),A0 / move.b (A0),-(SP)` at
 *                      0x00E57186 supplies its arg 5
 */
void DIR_$OLD_DELETE_FILEU(uid_t *dir_uid, char *name, uint16_t *name_len,
                           status_$t *status_ret, boolean *check_del_right,
                           boolean *no_lock)
{
    uint8_t buf[8];     /* A6-0x08, `pea (-0x8,A6)` at 0x00E57180 */

    /* 0x00E57184 `clr.w -(SP)`: allow_link is a constant FALSE here. */
    NAME_$OLD_DELETE_ENTRYU(dir_uid, name, *name_len,
                            *check_del_right, *no_lock, false,
                            buf, status_ret);
}
