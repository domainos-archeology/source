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

/*
 * Constant cells pooled just past FILE_$UNLOCK_PROC's `rts` and addressed
 * with `pea (d,PC)` (the PC used is the extension word, i.e. the instruction
 * address + 2).  Image bytes, `gsk read 0xE60FD0 8`:
 *
 *   00e60fd0  00 00 00 00 00 00 00 08
 *
 * so 0x00E60FD0 is a zero byte/word, 0x00E60FD2 a zero word and 0x00E60FD4
 * the longword 0x00000008.
 */

/*
 * 0x00E60FD0, byte 0x00.  ONE cell serving two callees, both of which read
 * it as a Domain boolean byte:
 *
 *   `pea (0x156,PC)` at 0x00E60E78 -> 0x00E60E7A + 0x156 = 0x00E60FD0
 *        PROC2_$FIND_ASID's second argument.  The callee tests it
 *        `movea.l (0xc,A6),A0` / `tst.b (A0)` / `bpl` at 0x00E40756, so
 *        FALSE asks for the plain ASID and skips the UPID indirection.
 *        The tree used to pass nil here, which the callee would have
 *        dereferenced.  (source-77ju)
 *
 *   `pea (0x116,PC)` at 0x00E60EB8 -> 0x00E60EBA + 0x116 = 0x00E60FD0
 *        ACL_$RIGHTS' ignore_super argument (FALSE - the super-user bypass
 *        applies).
 */
static boolean file_$unlock_proc_false_00e60fd0 = false;

/* 0x00E60FD2, word 0x0000: ACL_$RIGHTS' option flags.
 * `pea (0x120,PC)` at 0x00E60EB0 -> 0x00E60EB2 + 0x120. */
static int16_t file_$unlock_proc_acl_opts_00e60fd2 = 0;

/* 0x00E60FD4, longword 0x00000008: the required rights mask.
 * `pea (0x11e,PC)` at 0x00E60EB4 -> 0x00E60EB6 + 0x11E. */
static uint32_t file_$unlock_proc_rights_00e60fd4 = 0x00000008;

/*
 * The per-process lock count is reached at 0x00E60EDC-0x00E60EE8 with
 * `movea.l #0xea202c,A0` / `add.w D0w,D0w` / `lea (0x0,A0,D0w*0x1),A1` /
 * `move.w (0x1d98,A1),D0w`, i.e. 0xEA202C + asid*2 + 0x1D98 =
 * 0xEA3DC4 + asid*2 - which is FILE_$PROC_LOT_COUNT(asid) in
 * file/file_internal.h (the absolute cell on ARCH_M68K, FILE_$LOCK_TABLE2 on
 * a host build).  This file used to open-code the absolute address.
 */

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
        /* 0x00E60E76-0x00E60E88: three arguments, the middle one the zero
         * byte cell at 0x00E60FD0. */
        asid = PROC2_$FIND_ASID(proc_uid, (int8_t *)&file_$unlock_proc_false_00e60fd0,
                                status_ret);

        if (*status_ret != status_$ok) {
            /* 0x00E60E8E-0x00E60EA4: not found.  If the UID's node field names
             * THIS node the search is over - 0x00E60EA0 branches to
             * 0x00E60FC4, which is the `clr.l (A2)` the remote loop's tail
             * shares. */
            if ((proc_uid->low & 0xFFFFF) == NODE_$ME) {
                *status_ret = status_$ok;
                return;
            }
            /* 0x00E60EA4 `clr.w D2w`: search the lock table for the node. */
            asid = 0;
        }
    }

    /*
     * If unlocking for different process, check ACL rights
     */
    if (asid != PROC1_$AS_ID) {
        /* 0x00E60EAE-0x00E60EBE.  None of these four arguments may be NULL:
         * ACL_$RIGHTS dereferences all of them. */
        ACL_$RIGHTS(file_uid,
                    &file_$unlock_proc_false_00e60fd0,
                    &file_$unlock_proc_rights_00e60fd4,
                    &file_$unlock_proc_acl_opts_00e60fd2,
                    status_ret);
        if (*status_ret != status_$ok) {
            /* 0x00E60ECC-0x00E60ED2: the failing status is handed to
             * OS_PROC_SHUTWIRED and then returned unchanged. */
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
        /* 0x00E60EE8-0x00E60EF0: `subq.w #1` then `bmi` - an empty table
         * returns with the status untouched (it is still whatever
         * PROC2_$FIND_ASID or ACL_$RIGHTS left, i.e. zero). */
        count = (int16_t)FILE_$PROC_LOT_COUNT(asid) - 1;
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

            /* 0x00E60F16-0x00E60F1C */
            if (*status_ret != file_$object_not_locked_by_this_process) {
                return;
            }
        }

        /*
         * 0x00E60F26 `bra.w 0x00E60FC6`: when the `dbf` counter runs out the
         * image returns with the status STILL 0x000F0005 - it does not clear
         * it.  The tree used to fall through to a trailing
         * `*status_ret = status_$ok`, which reported success for a file that
         * was never found in the process's table.  (source-77ju)
         */
        return;
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

                        /* 0x00E60FAC-0x00E60FB4: only 0x000F0005 is turned
                         * into "keep going". */
                        if (*status_ret == file_$object_not_locked_by_this_process) {
                            *status_ret = status_$ok;
                        }
                    }
                }
            }
            /* 0x00E60FB6-0x00E60FB8: any non-zero status ends the walk. */
        } while (*status_ret == status_$ok);

        /*
         * 0x00E60FBC-0x00E60FC4: exactly ONE status is forgiven here -
         * 0x000F000C, the code FILE_$READ_LOCK_ENTRYI reports when the walk
         * runs off the end of the table.  Every other status is returned as
         * it stands; there is no trailing `clr.l (A2)` in the image.
         * (source-77ju)
         */
        if (*status_ret == file_$obj_not_locked_by_this_process) {
            *status_ret = status_$ok;
        }
    }
}
