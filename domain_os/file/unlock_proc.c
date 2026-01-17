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

/* External reference not in headers */
extern uint32_t NODE_$ME;

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
            FILE_$PRIV_UNLOCK(file_uid,
                              (uint16_t)slot,
                              ((uint32_t)*lock_mode << 16) | (uint32_t)asid,
                              0,                     /* local unlock */
                              0,                     /* param_5 */
                              0,                     /* param_6 */
                              dtv_out,               /* dtv_out */
                              status_ret);

            if (*status_ret != status_$file_object_not_locked_by_this_process) {
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
                         * Call FILE_$PRIV_UNLOCK with remote unlock flags
                         * remote_flags = -1 (0xFF prefix = remote unlock)
                         */
                        FILE_$PRIV_UNLOCK(file_uid,
                                          0,                     /* lock_index = 0 (search) */
                                          (uint32_t)req_mode << 16,
                                          -1,                    /* remote_flags = -1 */
                                          lock_info.context,     /* context */
                                          lock_info.owner_node,  /* node address */
                                          dtv_out,               /* dtv_out */
                                          status_ret);

                        if (*status_ret == status_$file_object_not_locked_by_this_process) {
                            *status_ret = status_$ok;
                        }
                    }
                }
            }
        } while (*status_ret == status_$ok);

        /*
         * Map "not locked" to success (finished iteration)
         */
        if (*status_ret == status_$file_obj_not_locked_by_this_process) {
            *status_ret = status_$ok;
            return;
        }
    }

    *status_ret = status_$ok;
}
