/*
 * FILE_$CREATE - Create a new file
 *
 * Original address: 0x00E5D778
 *
 * Simple wrapper around FILE_$PRIV_CREATE that creates a default file
 * in the specified directory.
 */

#include "file/file_internal.h"
#include "uid/uid.h"

/*
 * FILE_$CREATE
 *
 * Creates a new file in the specified directory.
 *
 * Parameters:
 *   dir_uid      - UID of the directory to create the file in
 *   file_uid_ret - Receives the UID of the newly created file
 *   status_ret   - Receives operation status
 *
 * Assembly at 0x00E5D778:
 *   link.w A6,0x0
 *   move.l (0x10,A6),-(SP)    ; status_ret
 *   clr.l -(SP)               ; param_7 = NULL
 *   clr.w -(SP)               ; param_6 = 0
 *   clr.l -(SP)               ; param_5 = 0
 *   move.l (0xc,A6),-(SP)     ; file_uid_ret
 *   move.l (0x8,A6),-(SP)     ; dir_uid
 *   move.l #0xe1737c,-(SP)    ; param_2 = &UID_$NIL
 *   clr.w -(SP)               ; param_1 = 0 (default file type)
 *   bsr.w FILE_$PRIV_CREATE
 *   unlk A6
 *   rts
 */
void FILE_$CREATE(uid_t *dir_uid, uid_t *file_uid_ret, status_$t *status_ret)
{
    FILE_$PRIV_CREATE(0, &UID_$NIL, dir_uid, file_uid_ret, 0, 0, NULL, status_ret);
}
