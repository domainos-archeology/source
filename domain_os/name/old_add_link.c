/*
 * name_$old_add_link - add a directory entry pointing at an existing object
 *
 * Original address: 0x00E5674C, 506 bytes.
 *
 * The shared body behind DIR_$OLD_ADDU (0x00E5696E, hard_link = FALSE) and
 * DIR_$OLD_ADD_HARD_LINKU (0x00E5699E, hard_link = TRUE).  It locates both the
 * target object and the parent directory, checks the caller's rights on the
 * directory, and then either forwards the whole operation to the directory's
 * owning node or performs it locally.
 *
 * Frame (A6+):
 *   0x08 dir_uid    (long)  -> A2
 *   0x0C name       (long)  -> A3
 *   0x10 name_len   (word)
 *   0x12 file_uid   (long)  -> A4
 *   0x16 hard_link  (byte)  -> D3b
 *   0x18 status_ret (long)
 *
 * Locals (A6-):
 *   -0xD2 parsed_len   word     -0xD0 status         long
 *   -0xCC status2      long     -0xC8 loc_unused     long
 *   -0xC4 vol_uid      long     -0xC0 loc_file       0x20 bytes
 *   -0xA0 loc_dir      0x20 bytes                    -0x90 = &loc_dir.loc_info
 *   -0x80 parsed_name  0x20 bytes
 *   -0x60 attr_value   word     -0x28 lock_entry     0x20 bytes
 *
 * D2b is the Domain boolean "the target object exists" - set TRUE at
 * 0x00E56774 and cleared at 0x00E567AC when AST_$GET_LOCATION comes back
 * file_$object_not_found on a non-hard-link add.
 */

#include "name/name_internal.h"
#include "dir/dir_internal.h"

/*
 * Constant cells for the ACL_$RIGHTS call at 0x00E567C0 (source-ns3b).  All
 * three are `pea (d,PC)` operands into the code region:
 *
 *   0x00E567BA  pea (-0x1c94,PC) -> 0x00E54B28  byte 0x00
 *   0x00E567B6  pea (0x18e,PC)   -> 0x00E56946  longword 0x00000002
 *   0x00E567B2  pea (-0x1c8e,PC) -> 0x00E54B26  word 0x0001
 *
 * 0x00E54B26/0x00E54B28 are a pair shared with the rest of name/; the raw
 * bytes at 0x00E54B26 are 00 01 00 00.
 */

/* 0x00E54B28: ignore_super, FALSE - the super-user bypass applies. */
static const boolean name_$old_add_link_ignore_super_00e54b28 = false;

/* 0x00E56946: required rights mask - 0x02 (add/write an entry). */
static const uint32_t name_$old_add_link_rights_00e56946 = 0x00000002;

/* 0x00E54B26: ACL_$RIGHTS option-flags word - 1 = the object is a directory. */
static const int16_t name_$old_add_link_acl_opts_00e54b26 = 1;

/*
 * Status codes this function produces or tests.  The file_$ ones are declared
 * in file/file.h:
 *   0x000F0001 file_$object_not_found                    (0x00E567A0)
 *   0x000F0004 file_$comms_problem_with_remote_node      (0x00E5686A)
 *   0x000F0005 file_$object_not_locked_by_this_process   (0x00E56888)
 *   0x000F0003 file_$bad_reply_received_from_remote_node (0x00E568B2)
 *   0x000F000B file_$op_cannot_perform_here              (0x00E568BC)
 */
/* 0x00E5683E `move.l #0xe000b` is status_$naming_invalid_leaf (name/name.h). */

/*
 * AST_$SET_ATTRIBUTE selectors used here (`move.w #0x6` at 0x00E568EA and
 * `move.w #0x7` at 0x00E56928): 6 bumps the object's hard-link count, 7 backs
 * that out again.
 */
#define AST_ATTR_LINK_COUNT_UP      6
#define AST_ATTR_LINK_COUNT_DOWN    7

void name_$old_add_link(uid_t *dir_uid, char *name, uint16_t name_len,
                        uid_t *file_uid, boolean hard_link,
                        status_$t *status_ret)
{
    file_$obj_loc_t loc_file;       /* A6-0xC0 */
    file_$obj_loc_t loc_dir;        /* A6-0xA0 */
    uint8_t   parsed_name[0x20];    /* A6-0x80 */
    uint8_t   lock_entry[0x20];     /* A6-0x28 */
    uint32_t  loc_unused;           /* A6-0xC8 */
    uint32_t  vol_uid;              /* A6-0xC4 */
    uint16_t  parsed_len;           /* A6-0xD2 */
    uint16_t  attr_value;           /* A6-0x60 */
    status_$t status;               /* A6-0xD0 */
    status_$t status2;              /* A6-0xCC */
    boolean   target_exists;        /* D2b */

    /* 0x00E56764-0x00E5676E: seed the target's location record. */
    loc_file.uid = *file_uid;
    loc_file.flags &= (int8_t)~FILE_OBJ_LOC_SCRATCH;

    target_exists = true;                               /* 0x00E56774 st D2b */

    AST_$GET_LOCATION(&loc_file, 0, &loc_unused, &vol_uid, &status);

    /* 0x00E56794-0x00E567AC */
    if (status != status_$ok) {
        if (hard_link < 0) {
            goto done;                                  /* 0x00E5679C */
        }
        if (status != file_$object_not_found) {
            goto done;                                  /* 0x00E567A8 */
        }
        target_exists = false;                          /* 0x00E567AC */
    }

    /*
     * 0x00E567AE-0x00E567DA: the caller must hold "add entry" rights on the
     * PARENT DIRECTORY (option flags 1 = directory, mask 0x02).  ACL statuses
     * are translated into naming statuses before they escape.
     */
    ACL_$RIGHTS(dir_uid,
                (boolean *)&name_$old_add_link_ignore_super_00e54b28,
                (uint32_t *)&name_$old_add_link_rights_00e56946,
                (int16_t *)&name_$old_add_link_acl_opts_00e54b26,
                &status);
    if (status != status_$ok) {
        NAME_CONVERT_ACL_STATUS(&status);               /* 0x00E567D4 */
        goto done;
    }

    /* 0x00E567DE-0x00E56810: now locate the directory itself. */
    loc_dir.uid = *dir_uid;
    loc_dir.flags &= (int8_t)~FILE_OBJ_LOC_SCRATCH;

    AST_$GET_LOCATION(&loc_dir, 0, &loc_unused, &vol_uid, &status);
    if (status != status_$ok) {
        goto done;
    }

    /*
     * 0x00E56814-0x00E568C4: the remote path.  Both the target must exist and
     * the directory must live on another node (`tst.b (-0x83,A6)` is
     * loc_dir.flags bit 7).
     */
    if (target_exists < 0 && loc_dir.flags < 0) {

        if (name_$validate_leaf(name, name_len, parsed_name, &parsed_len) >= 0) {
            status = status_$naming_invalid_leaf;       /* 0x00E5683E */
            goto done;
        }

        REM_FILE_$NAME_ADD_HARD_LINKU(&loc_dir.loc_info, dir_uid,
                                      (char *)parsed_name, parsed_len,
                                      file_uid, &status);

        /*
         * 0x00E5686A-0x00E568AE: a comms failure may just mean the lock entry
         * moved; re-read it and try once more.  status2 is a SEPARATE cell -
         * the retry decision looks at FILE_$READ_LOCK_ENTRYUI's status, not at
         * the remote call's.
         */
        if (status == file_$comms_problem_with_remote_node) {
            FILE_$READ_LOCK_ENTRYUI(dir_uid, lock_entry, &status2);
            if (status2 == file_$object_not_locked_by_this_process) {
                REM_FILE_$NAME_ADD_HARD_LINKU(&loc_dir.loc_info, dir_uid,
                                              (char *)parsed_name, parsed_len,
                                              file_uid, &status);
            }
        }

        /* 0x00E568B2-0x00E568C4 */
        if (status == file_$bad_reply_received_from_remote_node) {
            status = file_$op_cannot_perform_here;
        }
        goto done;
    }

    /* ------------------------- 0x00E568C6: local ------------------------- */
    attr_value = 1;

    /*
     * 0x00E568CC-0x00E568F6: bump the target's link count first, but only when
     * the target exists, sits on the same volume as the directory, and is
     * itself local.  `seq` + `and.b D2b,D0b` + `bmi` is the two-boolean AND.
     */
    if (loc_file.volume == loc_dir.volume && target_exists < 0 &&
        loc_file.flags >= 0) {
        AST_$SET_ATTRIBUTE(file_uid, AST_ATTR_LINK_COUNT_UP, &attr_value,
                           &status);
    }

    /* 0x00E568FA */
    if (status != status_$ok) {
        goto done;
    }

    /*
     * 0x00E56900-0x00E56914: the local add.  The word pushed at 0x00E5690C is
     * a constant zero - name_$old_add_link_local's second parameter.
     */
    name_$old_add_link_local(dir_uid, 0, name, name_len, file_uid, &status);

    /*
     * 0x00E56918-0x00E5692E: on failure back the link count out again.  The
     * rollback's status lands in status2 and is DISCARDED - the caller still
     * sees the add's own status.  There is no stack cleanup after this jsr;
     * the `movem`/`unlk` at 0x00E56938 drops the arguments.
     */
    if (status != status_$ok) {
        AST_$SET_ATTRIBUTE(file_uid, AST_ATTR_LINK_COUNT_DOWN, &attr_value,
                           &status2);
    }

done:                                                   /* 0x00E56934 */
    *status_ret = status;
}
