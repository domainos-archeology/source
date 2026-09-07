/*
 * FILE_$UNLOCK_PROC - Unlock a file on behalf of a process
 *
 * Original address: 0x00E60E3E
 * Size: 402 bytes
 *
 * Unlocks a file that was locked by a specified process.
 * This is used for administrative cleanup and process termination.
 *
 * Parameters:
 *   proc_uid   - UID of process whose locks to release (UID_$NIL = current)
 *   file_uid   - UID of file to unlock
 *   lock_mode  - Lock mode to release (0 = any mode)
 *   param_4    - Reserved/unused
 *   status_ret - Output status code
 *
 * Assembly analysis:
 *   - If proc_uid == UID_$NIL, uses current process (PROC1_$AS_ID)
 *   - Otherwise calls PROC2_$FIND_ASID to get ASID
 *   - If proc is remote node, asid=0 and searches lock entries
 *   - If local ASID != current, checks ACL rights first
 *   - For local ASID: iterates through process lock table calling PRIV_UNLOCK
 *   - For remote (asid=0): iterates with READ_LOCK_ENTRYI matching node
 */

#include "file/file_internal.h"
#include "proc/proc.h"
#include "acl/acl.h"

/* NODE_$ME is declared in network/network.h */

/* Per-process lock count table */
#define PROC_LOT_COUNT(asid) \
    (*(uint16_t *)((uint8_t *)0xEA3DC4 + (asid) * 2))

/*
 * FILE_$UNLOCK_PROC - Unlock a file on behalf of a process
 */
void FILE_$UNLOCK_PROC(uid_t *proc_uid, uid_t *file_uid, uint16_t *lock_mode,
                       uint32_t param_4, status_$t *status_ret)
{
    int16_t asid;
    int16_t count;
    int16_t slot;
    uint16_t iter_index;
    uint32_t dtv_out[2];
    uint16_t req_mode;

    /* Lock entry info buffer from FILE_$READ_LOCK_ENTRYI */
    file_lock_info_internal_t lock_info;

    /*
     * Determine ASID of target process
     */
    if ((proc_uid->high == UID_$NIL.high) && (proc_uid->low == UID_$NIL.low)) {
        /* UID_$NIL means current process */
        asid = PROC1_$AS_ID;
    } else {
        /* Find ASID for specified process */
        asid = PROC2_$FIND_ASID(proc_uid, NULL, status_ret);

        if (*status_ret != status_$ok) {
            /* If not found locally, check if it's this node */
            if ((proc_uid->low & 0xFFFFF) == NODE_$ME) {
                /* Process on this node - return success */
                *status_ret = status_$ok;
                return;
            }
            /* Remote process - use asid=0 for remote search */
            asid = 0;
        }
    }

    /*
     * If unlocking for different process, check ACL rights
     */
    if (asid != PROC1_$AS_ID) {
        ACL_$RIGHTS(file_uid, NULL, NULL, NULL, status_ret);
        if (*status_ret != status_$ok) {
            OS_PROC_SHUTWIRED(status_ret);
            return;
        }
    }

    /*
     * Process unlock based on ASID type
     */
    if (asid != 0) {
        /*
         * Local process - iterate through its lock table
         */
        count = PROC_LOT_COUNT(asid) - 1;
        if (count < 0) {
            return;
        }

        for (slot = 1; slot <= count + 1; slot++) {
            /*
             * 0x00E60EF6-0x00E60F0E: the slot number is sign-extended into a
             * longword (`move.w D3w,D0w; ext.l D0; move.l D0,-(SP)`).
             */
            (void)FILE_$PRIV_UNLOCK(file_uid,
                                    (int32_t)(int16_t)slot, /* lock_slot    */
                                    *lock_mode,             /* lock_mode    */
                                    (uint16_t)asid,         /* asid         */
                                    0,                      /* by_key       */
                                    0,                      /* key          */
                                    0,                      /* rem_key      */
                                    0,                      /* rem_node     */
                                    dtv_out,
                                    status_ret);

            if (*status_ret != file_$object_not_locked_by_this_process) {
                return;
            }
        }
    } else {
        /*
         * Remote process - iterate through lock entries looking for matching node
         */
        iter_index = 1;

        do {
            FILE_$READ_LOCK_ENTRYI(&UID_$NIL, &iter_index, &lock_info, status_ret);

            /*
             * Check if this lock entry matches the target process's node
             * and the requested file
             */
            if ((lock_info.owner_node & 0xFFFFF) == (proc_uid->low & 0xFFFFF)) {
                /*
                 * Node matches - check if status is OK and UID matches
                 */
                if ((*status_ret == status_$ok) &&
                    (lock_info.file_uid.high == file_uid->high) &&
                    (lock_info.file_uid.low == file_uid->low)) {

                    req_mode = *lock_mode;

                    /* Check mode matches (or mode=0 for any) */
                    if ((req_mode == lock_info.mode) || (req_mode == 0)) {
                        /*
                         * 0x00E60F88-0x00E60FA4, pushed right to left:
                         *   pea (A2)            status_ret
                         *   pea (-0x30,A6)      dtv_out
                         *   move.l (-0x1c,A6)   rem_node = lock_info.owner_node
                         *   move.l (-0x20,A6)   rem_key  = lock_info.context
                         *   move.w (-0x14,A6)   key      = lock_info.sequence
                         *   st                  by_key   = TRUE
                         *   clr.w               asid     = 0
                         *   move.w D4w          lock_mode = *lock_mode
                         *   clr.l               lock_slot = 0
                         *   pea (A4)            file_uid
                         */
                        (void)FILE_$PRIV_UNLOCK(file_uid,
                                                0,                      /* lock_slot */
                                                req_mode,               /* lock_mode */
                                                0,                      /* asid      */
                                                -1,                     /* by_key    */
                                                lock_info.sequence,     /* key       */
                                                lock_info.context,      /* rem_key   */
                                                lock_info.owner_node,   /* rem_node  */
                                                dtv_out,
                                                status_ret);

                        if (*status_ret == file_$object_not_locked_by_this_process) {
                            *status_ret = status_$ok;
                        }
                    }
                }
            }
        } while (*status_ret == status_$ok);

        /*
         * Map "not locked" to success (finished iteration)
         */
        if (*status_ret == file_$obj_not_locked_by_this_process) {
            *status_ret = status_$ok;
            return;
        }
    }

    *status_ret = status_$ok;
}
