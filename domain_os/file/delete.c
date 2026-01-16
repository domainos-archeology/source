/*
 * FILE_$DELETE and related delete functions
 *
 * These are wrapper functions around FILE_$DELETE_INT, the internal
 * delete handler. They provide different semantics for file deletion.
 */

#include "file/file_internal.h"

/*
 * FILE_$DELETE - Delete a file
 *
 * Original address: 0x00E5DB50
 *
 * Assembly:
 *   link.w A6,-0x4
 *   pea (A5)
 *   lea (0xe82128).l,A5
 *   subq.l #0x2,SP
 *   move.l (0xc,A6),-(SP)      ; status_ret
 *   pea (-0x2,A6)              ; local result buffer
 *   move.w #0x3,-(SP)          ; flags = 3 (delete + force)
 *   move.l (0x8,A6),-(SP)      ; file_uid
 *   bsr.w FILE_$DELETE_INT
 *   movea.l (-0x8,A6),A5
 *   unlk A6
 *   rts
 */
void FILE_$DELETE(uid_t *file_uid, status_$t *status_ret)
{
    uint8_t result;
    FILE_$DELETE_INT(file_uid, 3, &result, status_ret);
}

/*
 * FILE_$DELETE_OBJ - Delete a file object
 *
 * Original address: 0x00E5DB12
 *
 * Uses flags 5 (delete + delete-on-unlock) or 7 (delete + force + delete-on-unlock)
 * depending on the force parameter.
 */
void FILE_$DELETE_OBJ(uid_t *file_uid, int8_t force, void *param_3, status_$t *status_ret)
{
    uint16_t flags;

    if (force < 0) {
        flags = 7;  /* delete + force + delete-on-unlock */
    } else {
        flags = 5;  /* delete + delete-on-unlock */
    }

    FILE_$DELETE_INT(file_uid, flags, (uint8_t *)param_3, status_ret);
}

/*
 * FILE_$DELETE_FORCE - Force delete a file
 *
 * Original address: 0x00E5DB7A
 *
 * Note: Despite the name suggesting different behavior from FILE_$DELETE,
 * the assembly shows it passes the same flags (3). The distinction may be
 * semantic or for API compatibility.
 */
void FILE_$DELETE_FORCE(uid_t *file_uid, status_$t *status_ret)
{
    uint8_t result;
    FILE_$DELETE_INT(file_uid, 3, &result, status_ret);
}

/*
 * FILE_$DELETE_WHEN_UNLOCKED - Delete a file when unlocked
 *
 * Original address: 0x00E5FC3A
 *
 * Note: Assembly shows same flags (3) as FILE_$DELETE. The "when unlocked"
 * behavior may be handled by the internal delete logic based on lock state.
 */
void FILE_$DELETE_WHEN_UNLOCKED(uid_t *file_uid, status_$t *status_ret)
{
    uint8_t result;
    FILE_$DELETE_INT(file_uid, 3, &result, status_ret);
}

/*
 * FILE_$DELETE_FORCE_WHEN_UNLOCKED - Force delete when unlocked
 *
 * Original address: 0x00E5FC64
 */
void FILE_$DELETE_FORCE_WHEN_UNLOCKED(uid_t *file_uid, status_$t *status_ret)
{
    uint8_t result;
    FILE_$DELETE_INT(file_uid, 3, &result, status_ret);
}

/*
 * FILE_$REMOVE_WHEN_UNLOCKED - Remove a file when unlocked
 *
 * Original address: 0x00E5FC8E
 *
 * Unlike the other delete functions, this one returns the result byte
 * to the caller.
 */
void FILE_$REMOVE_WHEN_UNLOCKED(uid_t *file_uid, uint8_t *result, status_$t *status_ret)
{
    uint8_t local_result;
    FILE_$DELETE_INT(file_uid, 3, &local_result, status_ret);
    *result = local_result;
}
