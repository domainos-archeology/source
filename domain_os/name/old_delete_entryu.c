/*
 * NAME_$OLD_DELETE_ENTRYU - shared delete / drop-entry helper
 *
 * Original address: 0x00E56B08, 812 bytes.
 *
 * The body behind DIR_$OLD_DELETE_FILEU (0x00E56E5A), DIR_$OLD_DROP_HARD_LINKU
 * (0x00E56E34-ish wrapper) and one call from DIR_$OLD_ADD_BAKU.  It looks the
 * entry up, checks the caller's rights on the directory and then on the
 * object, deletes or remotely drops the object, and finally unlinks the
 * directory entry.
 *
 * Frame (A6+):
 *   0x08 dir_uid     (long)  -> A2
 *   0x0C name        (long)  -> A3
 *   0x10 name_len    (word)  passed BY ADDRESS to DIR_$OLD_GET_ENTRYU
 *   0x12 check_del_right (byte) -> D5b
 *   0x14 no_lock     (byte)  -> D4b
 *   0x16 allow_link  (byte)  -> D3b
 *   0x18 result_buf  (long)
 *   0x1C status_ret  (long)  -> A4
 *
 * Locals (A6-):
 *   -0xEE del_flag     byte  FILE_$DELETE_OBJ's out flag
 *   -0xEA lock_rights  word  FILE_$PRIV_LOCK's rights_out
 *   -0xE8 parsed_len   word  -0xE6 drop_flag word
 *   -0xE4 ast_status   long  -0xE0 lock_status long
 *   -0xDC/-0xD8              AST_$GET_LOCATION's two scratch cells
 *   -0xD4 dtv_out      8     -0xCC lock_slot long
 *   -0xC8 entry        dir_$old_entry_t (type word + UID)
 *   -0xB0 obj_uid      8
 *   -0xA8 loc_obj      0x20 bytes; -0x98 = &loc_obj.loc_info
 *   -0x88 loc_dir      0x20 bytes
 *   -0x60 parsed_name  0x20 bytes
 *   -0x40 cattr        0x18 bytes (ast_$common_attr_t)
 *   -0x28 lock_entry   0x20 bytes
 *
 * D6b is the Domain boolean "the object is where the directory says it is";
 * it is also reused as the constant 1 from 0x00E56CC8 onwards.
 */

#include "name/name_internal.h"
#include "dir/dir_internal.h"

/*
 * Constant cells for the two ACL_$RIGHTS calls (source-kr90).  Both are
 * `pea (d,PC)` operands resolving to extension-word address + displacement:
 *
 * Directory check, 0x00E56B7C:
 *   0x00E56B76  pea (-0x2050,PC) -> 0x00E54B28  byte 0x00
 *   0x00E56B72  pea (0x2c2,PC)   -> 0x00E56E36  longword 0x00000003
 *   0x00E56B6E  pea (-0x204a,PC) -> 0x00E54B26  word 0x0001
 *
 * Object check, 0x00E56C7E:
 *   0x00E56C76  pea (-0x2150,PC) -> 0x00E54B28  byte 0x00
 *   0x00E56C72  pea (0x1c6,PC)   -> 0x00E56E3A  longword 0x00000048
 *   0x00E56C6E  pea (0x1c4,PC)   -> 0x00E56E34  word 0xFFFF
 *
 * Raw bytes at 0x00E56E34: ff ff 00 00 00 03 00 00 00 48, so the two masks
 * and the two option words all live in one 10-byte pool.
 */

/* 0x00E54B28: ignore_super, FALSE - the super-user bypass applies.  Shared
 * with name_$old_add_link's call at 0x00E567C0. */
static const boolean name_$old_delete_entryu_ignore_super_00e54b28 = false;

/* 0x00E54B26: option flags 1 = the object being checked is a directory. */
static const int16_t name_$old_delete_entryu_dir_opts_00e54b26 = 1;

/* 0x00E56E36: rights needed on the PARENT directory - 0x03. */
static const uint32_t name_$old_delete_entryu_dir_rights_00e56e36 = 0x00000003;

/* 0x00E56E34: option flags 0xFFFF - acl_$eval_rights skips its object-type
 * check entirely (0x00E466A8). */
static const int16_t name_$old_delete_entryu_obj_opts_00e56e34 = -1;

/* 0x00E56E3A: rights needed on the OBJECT - bit 3 (0x08) or bit 6 (0x40). */
static const uint32_t name_$old_delete_entryu_obj_rights_00e56e3a = 0x00000048;

/*
 * Bits of the rights word the object check looks at (`btst.l #0x3` at
 * 0x00E56CA2 and `btst.l #0x6` at 0x00E56CAC) - the same pair
 * dir_$do_op_delete uses.
 */
#define NAME_DELETE_RIGHT_DELETE     0x00000008U
#define NAME_DELETE_RIGHT_PROTECTED  0x00000040U

/* AST_$GET_COMMON_ATTRIBUTES flag word (`move.w #0x28` at 0x00E56BF0). */
#define NAME_DELETE_CATTR_FLAGS      0x0028

/* Entry types DIR_$OLD_GET_ENTRYU reports. */
#define NAME_OLD_ENTRY_FILE          1
#define NAME_OLD_ENTRY_LINK          3

void NAME_$OLD_DELETE_ENTRYU(uid_t *dir_uid, char *name, uint16_t name_len,
                             boolean check_del_right, boolean no_lock,
                             boolean allow_link, uint8_t *result_buf,
                             status_$t *status_ret)
{
    dir_$old_entry_t   entry;           /* A6-0xC8 */
    uid_t              obj_uid;         /* A6-0xB0 */
    file_$obj_loc_t    loc_dir;         /* A6-0x88 */
    file_$obj_loc_t    loc_obj;         /* A6-0xA8 */
    ast_$common_attr_t cattr;           /* A6-0x40 */
    uint8_t   parsed_name[0x20];        /* A6-0x60 */
    uint8_t   lock_entry[0x20];         /* A6-0x28 */
    uint32_t  dtv_out[2];               /* A6-0xD4 */
    uint32_t  loc_unused;               /* A6-0xDC */
    uint32_t  vol_uid;                  /* A6-0xD8 */
    uint32_t  lock_slot;                /* A6-0xCC */
    uint16_t  lock_rights;              /* A6-0xEA */
    uint16_t  parsed_len;               /* A6-0xE8 */
    uint16_t  drop_flag;                /* A6-0xE6 */
    status_$t ast_status;               /* A6-0xE4 */
    status_$t lock_status;              /* A6-0xE0 */
    int8_t    del_flag;                 /* A6-0xEE */
    boolean   same_location;            /* D6b */
    int16_t   entry_type;               /* D0w at 0x00E56B44 */
    int16_t   sub_type;                 /* D2w at 0x00E56C04 */
    uint32_t  rights;                   /* D0 from ACL_$RIGHTS */
    status_$t acl_status;               /* D1 at 0x00E56C88 */

    /* 0x00E56B28-0x00E56B40 */
    DIR_$OLD_GET_ENTRYU(dir_uid, name, &name_len, &entry, status_ret);
    if (*status_ret != status_$ok) {
        return;
    }

    /*
     * 0x00E56B44-0x00E56B68: only a plain file, or a link when the caller
     * allows one, may be deleted here.
     */
    entry_type = (int16_t)entry.type;
    if (entry_type != NAME_OLD_ENTRY_FILE) {
        if (entry_type == NAME_OLD_ENTRY_LINK) {
            if (allow_link >= 0) {
                *status_ret = status_$naming_invalid_link_operation;
            }
        } else {
            *status_ret = status_$naming_invalid_leaf;
        }
        if (*status_ret != status_$ok) {
            return;
        }
    }

    /* 0x00E56B6C-0x00E56B88: rights on the parent directory. */
    ACL_$RIGHTS(dir_uid,
                (boolean *)&name_$old_delete_entryu_ignore_super_00e54b28,
                (uint32_t *)&name_$old_delete_entryu_dir_rights_00e56e36,
                (int16_t *)&name_$old_delete_entryu_dir_opts_00e54b26,
                status_ret);
    if (*status_ret != status_$ok) {
        goto convert_acl_status;                        /* 0x00E56B88 */
    }

    /* 0x00E56B8C-0x00E56BC2: where does the directory live? */
    loc_dir.uid = *dir_uid;
    loc_dir.flags &= (int8_t)~FILE_OBJ_LOC_SCRATCH;
    AST_$GET_LOCATION(&loc_dir, 0, &loc_unused, &vol_uid, &ast_status);
    if (ast_status != status_$ok) {
        *status_ret = ast_status;
        return;
    }

    /* 0x00E56BC6-0x00E56BE4: and where does the object live? */
    obj_uid = entry.uid;
    loc_obj.uid = obj_uid;
    loc_obj.flags &= (int8_t)~FILE_OBJ_LOC_SCRATCH;
    same_location = true;                               /* 0x00E56BE4 st D6b */

    AST_$GET_COMMON_ATTRIBUTES(&loc_obj, NAME_DELETE_CATTR_FLAGS, &cattr,
                               &ast_status);

    /*
     * 0x00E56C02-0x00E56C3E.  The object is "where the directory says it is"
     * unless
     *   (a) it is a directory (sub-type 2) the caller allows links to AND the
     *       parent is the root (NAME_$ROOT_UID at 0xE8029C), or
     *   (b) the two location records disagree on the volume or the node, or
     *   (c) the attribute fetch failed.
     */
    sub_type = (int16_t)cattr.sub_type;
    if ((sub_type == 2 && allow_link < 0 &&
         dir_uid->high == NAME_$ROOT_UID.high &&
         dir_uid->low  == NAME_$ROOT_UID.low) ||
        loc_obj.volume != loc_dir.volume ||
        ast_status != status_$ok ||
        loc_obj.node != loc_dir.node) {

        /* 0x00E56C40-0x00E56C52 */
        *status_ret = ast_status;
        if (ast_status != status_$ok &&
            ast_status != file_$object_not_found) {
            return;
        }
        same_location = false;
    }

    /* 0x00E56C54: an object we cannot see is simply unlinked. */
    if (same_location >= 0) {
        goto drop_entry;
    }

    /* 0x00E56C5A-0x00E56C68 */
    if (allow_link >= 0 && sub_type != 0) {
        *status_ret = status_$naming_name_is_not_a_file;
        return;
    }

    /* 0x00E56C6C-0x00E56C84: rights on the object itself. */
    rights = ACL_$RIGHTS(&obj_uid,
                         (boolean *)&name_$old_delete_entryu_ignore_super_00e54b28,
                         (uint32_t *)&name_$old_delete_entryu_obj_rights_00e56e3a,
                         (int16_t *)&name_$old_delete_entryu_obj_opts_00e56e34,
                         status_ret);

    /* 0x00E56C88-0x00E56CB8: the same rights arithmetic dir_$do_op_delete
     * uses - `not.b` on the caller's flag OR'ed with "bit 3 is clear". */
    acl_status = *status_ret;
    if (acl_status != status_$insufficient_rights_to_perform_operation &&
        acl_status != status_$ok &&
        acl_status != status_$no_right_to_perform_operation) {
        goto convert_acl_status;
    }
    if (check_del_right >= 0 || (rights & NAME_DELETE_RIGHT_DELETE) == 0) {
        if ((rights & NAME_DELETE_RIGHT_PROTECTED) != 0) {
            *status_ret = status_$naming_insufficient_rights;
            return;
        }
    }

    /*
     * 0x00E56CC8-0x00E56CE0: a directory (sub-type 1 or 2) with no more than
     * one reference cannot be dropped this way.  `cmp.w (-0x2c,A6),D6w` with
     * D6 = 1 and `bcs` is an UNSIGNED "1 < refcount".
     */
    if (cattr.refcount <= 1 && (sub_type == 1 || sub_type == 2)) {
        *status_ret = status_$naming_no_rights;
        return;
    }

    /* 0x00E56CE4: local objects are deleted outright. */
    if (loc_obj.flags >= 0) {
        goto local_delete;
    }

    /* ------------------- 0x00E56CEC: the remote path ------------------- */
    if (name_$validate_leaf(name, name_len, parsed_name, &parsed_len) >= 0) {
        *status_ret = status_$naming_invalid_leaf;
        return;
    }

    /*
     * 0x00E56D12-0x00E56D4E.  `move.l #0x40000` is the merged lock_mode /
     * local_only pair: mode 4, local_only FALSE; the preceding `clr.w` is the
     * `side` word.
     */
    if (no_lock >= 0) {
        FILE_$PRIV_LOCK(&obj_uid,
                        (int16_t)PROC1_$AS_ID,      /* 0x00E2060A */
                        0,                          /* side */
                        4,                          /* lock_mode */
                        false,                      /* local_only */
                        0, 0,                       /* flags, key */
                        0, 0, 0,                    /* rem_key/node/extra */
                        (void **)&NAME_$CONST_ZERO_L,     /* 0x00E54730, NIL */
                        0,                          /* rem_wait */
                        &lock_slot, &lock_rights, status_ret);
        if (*status_ret != status_$ok) {
            return;
        }
    }

    /* 0x00E56D52-0x00E56D5E: D6 still holds the constant 1. */
    drop_flag = (check_del_right < 0) ? 1 : 0;

    REM_FILE_$DROP_HARD_LINKU(&loc_obj.loc_info, dir_uid,
                              (char *)parsed_name, parsed_len, drop_flag,
                              status_ret);

    /* 0x00E56D7E-0x00E56DBE: one retry after a comms failure, gated on the
     * lock entry having gone away. */
    if (*status_ret == file_$comms_problem_with_remote_node) {
        FILE_$READ_LOCK_ENTRYUI(dir_uid, lock_entry, &lock_status);
        if (lock_status == file_$object_not_locked_by_this_process) {
            REM_FILE_$DROP_HARD_LINKU(&loc_obj.loc_info, dir_uid,
                                      (char *)parsed_name, parsed_len,
                                      drop_flag, status_ret);
        }
    }

    /* 0x00E56DC2-0x00E56DEC */
    if (no_lock >= 0) {
        (void)FILE_$PRIV_UNLOCK(&obj_uid, (int32_t)lock_slot, 4,
                                (uint16_t)PROC1_$AS_ID,
                                false, 0, 0, 0, dtv_out, &ast_status);
    }

    /* 0x00E56DF0-0x00E56DF8: the remote path never falls through to the
     * unlink - it reports the unlock's status instead. */
    if (*status_ret != status_$ok) {
        return;
    }
    *status_ret = ast_status;
    return;

local_delete:                                           /* 0x00E56DFA */
    FILE_$DELETE_OBJ(&obj_uid, no_lock, &del_flag, status_ret);
    if (*status_ret != status_$ok) {
        return;
    }

drop_entry:                                             /* 0x00E56E16 */
    /* The word pushed at 0x00E56E1C is a constant zero - the lock mode. */
    name_$old_drop_entry(dir_uid, name, name_len, 0, result_buf, status_ret);
    return;

convert_acl_status:                                     /* 0x00E56CBC */
    NAME_CONVERT_ACL_STATUS(status_ret);
}
