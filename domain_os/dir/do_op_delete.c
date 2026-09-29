/*
 * dir_$do_op_delete - DO_OP handler for delete / drop-link operations
 *
 * Original address: 0x00E5125E, 860 bytes.
 *
 * The server-side body behind three DIR_$DO_OP opcodes:
 *   0x2E  delete file, with the "check delete right" flag from request+0x91
 *         bit 0                                            (call 0x00E4C3CA)
 *   0x30  drop hard link - all three flags TRUE            (call 0x00E4C416)
 *   0x36  delete file, flags taken from request+0x90/+0x91,
 *         allow_link FALSE                                 (call 0x00E4C51E)
 *
 * Frame (A6+):
 *   0x08 dir_uid          (long)
 *   0x0C name             (long)
 *   0x10 name_len         (word)
 *   0x12 check_del_right  (byte)  -> D4b
 *   0x14 entry_only       (byte)  -> D5b
 *   0x16 allow_link       (byte)  -> D3b
 *   0x18 entry_uid_ret    (long)  -> A3
 *   0x1C deleted_uid_ret  (long)
 *   0x20 status_ret       (long)  -> A2
 *
 * Locals (A6-):
 *   -0x96 deleted        byte   FILE_$DELETE_OBJ's out flag
 *   -0x92 drop_object    byte   Domain boolean, `st` at 0x00E51292
 *   -0x90 lock_rights    word   FILE_$PRIV_LOCK's rights_out
 *   -0x8E/-0x8C          dir_$find_entry's two trailing out cells
 *   -0x88 handle         long
 *   -0x84 entry          long   dir_$find_entry's entry pointer
 *   -0x80 ast_status     long
 *   -0x7C lock_slot      long   FILE_$PRIV_LOCK's slot_io
 *   -0x78 cattr          0x18 bytes (ast_$common_attr_t)
 *   -0x60 loc            0x20 bytes (file_$obj_loc_t); loc.uid is at -0x58
 *   -0x40 scratch_uid    8 bytes (the second dir_$remove_entry's uid_ret)
 *   -0x38 attr_value     word
 *
 * D6b is the Domain boolean "we took a lock on the object" and is cleared at
 * 0x00E51290; A5 is DIR_$DO_OP's module base 0xE7DC00.
 */

#include "dir/dir_internal.h"

/*
 * Constant cells for the ACL_$RIGHTS call at 0x00E51462 (source-6dil).  All
 * three are `pea (d,PC)` operands; the effective address is the extension
 * word's address plus the displacement:
 *
 *   0x00E5145A  pea (-0x4468,PC) -> 0x00E4CFF4  byte 0x00
 *   0x00E51456  pea (-0xe92,PC)  -> 0x00E505C6  longword 0x00000048
 *   0x00E51452  pea (-0xe90,PC)  -> 0x00E505C4  word 0xFFFF
 *
 * The raw bytes at 0x00E505C4 are ff ff 00 00 00 48, so the word and the
 * longword overlap in the pool exactly as the two displacements suggest.
 */

/* 0x00E4CFF4: ignore_super, FALSE - the super-user bypass applies. */
static const boolean dir_$do_op_delete_ignore_super_00e4cff4 = false;

/* 0x00E505C6: required rights mask - bit 3 (0x08) or bit 6 (0x40). */
static const uint32_t dir_$do_op_delete_rights_00e505c6 = 0x00000048;

/* 0x00E505C4: ACL_$RIGHTS option-flags word.  0xFFFF makes acl_$eval_rights
 * skip its object-type check entirely (0x00E466A8). */
static const int16_t dir_$do_op_delete_acl_opts_00e505c4 = -1;

/*
 * 0x00E51504 `pea (-0x61ca,PC)` -> 0x00E4F33C, the pointer FILE_$PRIV_LOCK
 * receives as its `acl_ctx` argument.  FILE_$PRIV_LOCK_$CHECK_RIGHTS only
 * dereferences it when the flags word has FILE_LOCK_FLAG_REMOTE set
 * (`btst.l #0x1` at 0x00E5EDD8, then `movea.l (0x24,A3),A0 / movea.l (A0),A4`
 * at 0x00E5EDDE), and this call site passes flags = 0.  The image's operand
 * is an address inside the code region, i.e. a pooled cell that is never
 * read on this path; a file-static cell stands in for it here.
 */
static uid_t *dir_$do_op_delete_acl_ctx_00e4f33c;

/*
 * 0x00E515BA, the word immediately after this function: FILE_$UNLOCK_D's
 * lock_mode argument.  Raw bytes 00 04.
 */
static const uint16_t dir_$do_op_delete_unlock_mode_00e515ba = 4;

/*
 * Bits of the rights word ACL_$RIGHTS returns that this function tests
 * (`btst.l #0x3` at 0x00E51486 and `btst.l #0x6` at 0x00E51490).
 */
#define DIR_DELETE_RIGHT_DELETE     0x00000008U
#define DIR_DELETE_RIGHT_PROTECTED  0x00000040U

/*
 * ast_$common_attr_t.access_flags bits, read here as the WORD at cattr+0x16
 * (`move.w (-0x62,A6),D0w` at 0x00E513AE), so `btst.l #0xe` is bit 6 of the
 * byte and `tst.w`/`bpl` is bit 7.
 */
#define DIR_CATTR_ACCESS_REFCOUNTED 0x40    /* bit 6 -> btst #0xE of the word */

/* AST_$GET_COMMON_ATTRIBUTES flag word (`move.w #0x81` at 0x00E5136C). */
#define DIR_DELETE_CATTR_FLAGS      0x0081

/* AST_$SET_ATTRIBUTE selector 7: drop the object's reference count. */
#define AST_ATTR_LINK_COUNT_DOWN    7

/* Entry type is the low three bits of the entry's first byte
 * (`moveq #0x7,D0 / and.b (A0),D0b` at 0x00E51306). */
#define DIR_ENTRY_TYPE_MASK         0x07
#define DIR_ENTRY_TYPE_SOFT_LINK    3
#define DIR_ENTRY_TYPE_MOUNT        4

/*
 * Status codes (SR10.4 status database).  The naming ones
 * (status_$naming_name_not_found, _invalid_link_operation,
 * _name_is_not_a_file, _no_rights, _insufficient_rights, _directory_locked)
 * come from name/name.h.
 */

void dir_$do_op_delete(uid_t *dir_uid, void *name, uint16_t name_len,
                       boolean check_del_right, boolean entry_only,
                       boolean allow_link, uid_t *entry_uid_ret,
                       uid_t *deleted_uid_ret, status_$t *status_ret)
{
    char *blk = DIR_$BLOCK;   /* the routine's own A5 = 0x00E7DC00 */
    void     *handle;                   /* A6-0x88 */
    uint8_t  *entry;                    /* A6-0x84 */
    file_$obj_loc_t   loc;              /* A6-0x60 */
    ast_$common_attr_t cattr;           /* A6-0x78 */
    uid_t     scratch_uid;              /* A6-0x40 */
    uint32_t  lock_slot;                /* A6-0x7C */
    uint16_t  lock_rights;              /* A6-0x90 */
    uint16_t  attr_value;               /* A6-0x38 */
    uint16_t  find_extra;               /* A6-0x8E */
    uint32_t  find_depth;               /* A6-0x8C */
    status_$t ast_status;               /* A6-0x80 */
    int8_t    deleted;                  /* A6-0x96 */
    boolean   drop_object;              /* A6-0x92 */
    boolean   obj_locked;               /* D6b */
    char      found;                    /* D0b from dir_$find_entry */
    int16_t   entry_type;               /* D0w at 0x00E51306 */
    int16_t   sub_type;                 /* D2w at 0x00E513F0 */
    uint32_t  rights;                   /* D0 from ACL_$RIGHTS */
    status_$t acl_status;               /* D1 at 0x00E5146C */
    int16_t   i;

    /* 0x00E5127A-0x00E51288: the caller's "deleted object" cell starts NIL. */
    *deleted_uid_ret = UID_$NIL;

    ACL_$ENTER_SUPER();                                 /* 0x00E5128A */

    obj_locked  = false;                                /* 0x00E51290 clr.b D6b */
    drop_object = true;                                 /* 0x00E51292 st */

    /* 0x00E51296-0x00E512AA: `move.l #0x20003` is the merged mode/rights
     * pair - mode 2 (write), rights 3. */
    dir_$open_dir(dir_uid, 2, 3, &handle, status_ret);

    ACL_$EXIT_SUPER();                                  /* 0x00E512AE */

    if (*status_ret != status_$ok) {
        goto release_and_out;
    }

    /* 0x00E512BA-0x00E512DC */
    found = dir_$find_entry(handle, name, (int16_t)name_len, 0,
                            (void **)&entry, &find_extra,
                            (int16_t *)&find_depth);

    /* 0x00E512E0-0x00E51302: a type-9 process gets silence, everyone else
     * gets "name not found". */
    if (found >= 0) {
        if (PROC1_$DATA.type[PROC1_$CURRENT] == 9) {
            goto release_and_out;
        }
        *status_ret = status_$naming_name_not_found;
        goto release_and_out;
    }

    /* 0x00E51306-0x00E51338 */
    entry_type = (int16_t)(entry[0] & DIR_ENTRY_TYPE_MASK);
    if (entry_type == DIR_ENTRY_TYPE_MOUNT) {
        if (allow_link < 0) {
            dir_$remove_entry(handle, name, (int16_t)name_len, 0,
                              entry_uid_ret, status_ret);
            goto release_and_out;
        }
        *status_ret = status_$naming_invalid_link_operation;
        goto release_and_out;
    }

    /* 0x00E5133C-0x00E51344: the entry's UID goes back to the caller. */
    *entry_uid_ret = *(uid_t *)(entry + 4);

    /* 0x00E51346-0x00E5134E: dropping a soft link never touches an object. */
    if (allow_link < 0 && entry_type == DIR_ENTRY_TYPE_SOFT_LINK) {
        goto no_object;
    }

    /* 0x00E51350-0x00E5137A */
    loc.uid = *(uid_t *)(entry + 4);
    loc.flags &= (int8_t)~FILE_OBJ_LOC_SCRATCH;
    AST_$GET_COMMON_ATTRIBUTES(&loc, DIR_DELETE_CATTR_FLAGS, &cattr,
                               &ast_status);

    /*
     * 0x00E5137E-0x00E51390: the attributes are only usable when the fetch
     * succeeded AND the object still sits on the volume this directory
     * handle was validated against (handle+0x3A).
     */
    if (ast_status == status_$ok && loc.volume ==
            (uint16_t)*(int16_t *)((char *)handle + DIR_HANDLE_VOLUME_OFF)) {
        goto have_attrs;
    }

    /* 0x00E51392-0x00E513A2: a real error other than "object not found"
     * aborts; "not found" and success fall through to the entry-only path. */
    if (ast_status != status_$ok && ast_status != file_$object_not_found) {
        *status_ret = ast_status;
        goto release_and_out;
    }

no_object:                                              /* 0x00E513A6 */
    /* `move.b D6b,(-0x92,A6)`: obj_locked is still FALSE here, so this
     * clears drop_object. */
    drop_object = obj_locked;
    goto remove_entry;

have_attrs:                                             /* 0x00E513AE */
    /*
     * `move.w (-0x62,A6),D0w` reads cattr+0x16 as a WORD - access_flags in
     * the high byte, the never-written pad byte in the low one.  Both tests
     * that follow live in the high byte, so they are written against
     * access_flags directly: `btst.l #0xe` is its bit 6 and `tst.w`/`bpl` is
     * its bit 7 (i.e. the signed byte's sign).
     */

    /* 0x00E513B2-0x00E513C6: a reference-counted object with fewer than two
     * references may not be deleted through this path. */
    if ((cattr.access_flags & DIR_CATTR_ACCESS_REFCOUNTED) != 0 &&
        cattr.refcount < 2) {
        *status_ret = status_$no_rights;
        goto release_and_out;
    }

    /* 0x00E513CA-0x00E513EA: an OS-only object is off limits to type-9
     * processes. */
    if (cattr.access_flags < 0 && PROC1_$DATA.type[PROC1_$CURRENT] == 9) {
        *status_ret = status_$ast_only_local_access_allowed;
        goto release_and_out;
    }

    /*
     * 0x00E513EE-0x00E51412: `seq`/`or.b`/`or.b`/`or.b D3b`/`bmi` - the
     * sub-type must be 5, 4 or 0, unless the caller allows links.
     */
    sub_type = (int16_t)cattr.sub_type;
    if (!(sub_type == 5 || sub_type == 4 || sub_type == 0 || allow_link < 0)) {
        *status_ret = status_$naming_name_is_not_a_file;
        goto release_and_out;
    }

    /*
     * 0x00E51416-0x00E5144C: a directory (sub-type 1 or 2) that is the source
     * of a mount cannot be deleted.  The loop reloads &loc.uid every
     * iteration (`lea (-0x58,A6),A1` is the dbf target at 0x00E5142E).
     */
    if (sub_type == 2 || sub_type == 1) {
        for (i = (int16_t)(DIR_MOUNT_COUNT16(blk) - 1);
             i >= 0; i--) {
            /* A5 + 8 + 0x1554 + i*8 is mount table entry i + 1. */
            const uid_t *src = &DIR_MOUNT_UID_OF(blk, (int32_t)i + 1);
            if (src->high == loc.uid.high && src->low == loc.uid.low) {
                *status_ret = status_$naming_directory_locked;
                goto release_and_out;
            }
        }
    }

    /* 0x00E51450-0x00E51468 (source-6dil). */
    rights = ACL_$RIGHTS(&loc.uid,
                         (boolean *)&dir_$do_op_delete_ignore_super_00e4cff4,
                         (uint32_t *)&dir_$do_op_delete_rights_00e505c6,
                         (int16_t *)&dir_$do_op_delete_acl_opts_00e505c4,
                         status_ret);

    /*
     * 0x00E5146C-0x00E514AC.  "no right" and "insufficient rights" are not
     * fatal here - the rights word itself decides.  Anything else is
     * translated into a naming status and returned.
     */
    acl_status = *status_ret;
    if (acl_status == status_$insufficient_rights_to_perform_operation ||
        acl_status == status_$ok ||
        acl_status == status_$no_right_to_perform_operation) {
        /*
         * 0x00E51482-0x00E5149C: `move.b D4b,D7b / not.b D7b`, then
         * `btst.l #0x3,D0 / seq D3b / or.b D3b,D7b / bpl`.  Reading the
         * boolean algebra out: when the caller did not ask for the delete
         * right, or the object does not grant it, the "protected" bit 6 is
         * what refuses the operation.
         */
        if (check_del_right >= 0 || (rights & DIR_DELETE_RIGHT_DELETE) == 0) {
            if ((rights & DIR_DELETE_RIGHT_PROTECTED) != 0) {
                *status_ret = status_$naming_insufficient_rights;
                goto release_and_out;
            }
        }
    } else {
        NAME_CONVERT_ACL_STATUS(status_ret);            /* 0x00E514A2 */
        goto release_and_out;
    }

    /*
     * 0x00E514AE-0x00E514F0: a directory is not deleted here, only
     * dereferenced - AST_$SET_ATTRIBUTE selector 7 drops its reference count,
     * and "reference count says unused" becomes the naming "no rights".
     */
    if (sub_type == 2 || sub_type == 1) {
        attr_value = 1;
        AST_$SET_ATTRIBUTE(&loc.uid, AST_ATTR_LINK_COUNT_DOWN, &attr_value,
                           status_ret);
        if (*status_ret == status_$ast_refcount_says_unused) {
            *status_ret = status_$naming_no_rights;
            goto release_and_out;
        }
        drop_object = false;                            /* 0x00E514EC */
        goto remove_entry;
    }

    /*
     * 0x00E514F2-0x00E5152E: otherwise take the object's lock before
     * unlinking it, unless the caller only wants the entry removed.
     * `pea (0x4).w` is the merged side/lock_mode pair - side 0, mode 4.
     */
    if (entry_only >= 0) {
        FILE_$PRIV_LOCK(&loc.uid,
                        (int16_t)PROC1_$AS_ID,      /* 0x00E2060A */
                        0,                          /* side */
                        4,                          /* lock_mode */
                        true,                       /* local_only, `st` */
                        0, 0,                       /* flags, key */
                        0, 0, 0,                    /* rem_key/node/extra */
                        (void **)&dir_$do_op_delete_acl_ctx_00e4f33c,
                        0,                          /* rem_wait */
                        &lock_slot, &lock_rights, status_ret);
        if (*status_ret != status_$ok) {
            goto release_and_out;
        }
        obj_locked = true;                              /* 0x00E5152E st D6b */
    }

remove_entry:                                           /* 0x00E51530 */
    /* This second removal throws its UID away (`pea (-0x40,A6)`). */
    dir_$remove_entry(handle, name, (int16_t)name_len, 0, &scratch_uid,
                      status_ret);
    if (*status_ret != status_$ok) {
        goto release_and_out;
    }

    dir_$release_handle(&handle);                       /* 0x00E51554 */

    /* 0x00E5155A-0x00E5158A */
    if (drop_object < 0) {
        FILE_$DELETE_OBJ(&loc.uid, true, &deleted, status_ret);
        if (deleted < 0) {
            *deleted_uid_ret = loc.uid;
        }
    }

    /* 0x00E5158C-0x00E515A6.  The unlock's status goes into ast_status and
     * is discarded; the caller keeps the removal's status. */
    if (obj_locked < 0) {
        FILE_$UNLOCK_D(&loc.uid, &lock_slot,
                       (uint16_t *)&dir_$do_op_delete_unlock_mode_00e515ba,
                       &ast_status);
    }
    return;                                             /* 0x00E515B0 */

release_and_out:                                        /* 0x00E515A8 */
    dir_$release_handle(&handle);
}
