/*
 * DIR_$OLD_CREATE_DIRU - Legacy create subdirectory
 *
 * Original address: 0x00E571AE
 * Original size: 414 bytes (0x00E571AE-0x00E5734B)
 *
 * Re-derived from the disassembly (bead source-04ci).  A5 = 0x00E7FD24, the
 * NAME/DIR module block, so the four per-process directory-lock tables are the
 * NAME_$LOCK_* arrays declared in name/name.h.
 */

#include "dir/dir_internal.h"

/*
 * DIR_$OLD_CREATE_DIRU - Legacy create subdirectory
 *
 * Parameters (0x08/0x0c/0x10/0x14/0x18 in the image's frame):
 *   parent_uid  - (0x08,A6) -> A2, UID of the parent directory
 *   name        - (0x0c,A6) leaf name to create
 *   name_len    - (0x10,A6) pointer to the name length word
 *   new_dir_uid - (0x14,A6) -> A3, output: UID of the created directory
 *   status_ret  - (0x18,A6) -> A4, output: status code
 *
 * The routine locks the parent, then saves the caller's per-process lock state
 * (0x00E5720E-0x00E57246) because dir_$old_create_obj locks and maps the NEW
 * directory through the same per-process slots; it is restored verbatim at
 * 0x00E572EE-0x00E57326 before the parent is unlocked.
 */
void DIR_$OLD_CREATE_DIRU(uid_t *parent_uid, char *name, uint16_t *name_len,
                          uid_t *new_dir_uid, status_$t *status_ret)
{
    /* Frame is link.w A6,-0x54; the offsets below are the image's. */
    uint8_t   parsed_name[48];      /* A6-0x38 .. A6-0x09 */
    uint16_t  parsed_len;           /* A6-0x50 */
    uint32_t  handle;               /* A6-0x4c */
    status_$t unlock_status;        /* A6-0x48 */
    uint8_t   add_result[4];        /* A6-0x44 */
    uint8_t   truncate_result;      /* A6-0x52 */
    int8_t    leaf_ok;              /* D0.b from name_$validate_leaf */

    /* Saved per-process lock state (A6-0x08 .. A6-0x01 plus D4/D3/D2) */
    uid_t     saved_lock_uid;
    uint32_t  saved_lock_handle;
    int16_t   saved_lock_mode;
    uint32_t  saved_lock_slot;

    /* 0x00E571C8-0x00E571E6 */
    leaf_ok = name_$validate_leaf(name, *name_len, parsed_name, &parsed_len);
    if (leaf_ok >= 0) {
        /* 0x00E571E8: not a valid leaf */
        *status_ret = status_$naming_invalid_leaf;
        return;                     /* bra 0x00E57342 - no ACL_$EXIT_SUPER */
    }

    /* 0x00E571F2: pushed longword 0x00040002 == lock_mode 4, acl_rights 2 */
    NAME_$LOCK_DIR(parent_uid, &handle, 4, 2, status_ret);
    if (*status_ret != status_$ok) {        /* 0x00E57208: tst.l (A4) */
        ACL_$EXIT_SUPER();                  /* 0x00E5733C */
        return;
    }

    /* 0x00E5720E-0x00E57246: save the caller's per-process lock state */
    saved_lock_uid    = NAME_$LOCK_UID[PROC1_$CURRENT];
    saved_lock_handle = NAME_$LOCK_HANDLE[PROC1_$CURRENT];
    saved_lock_mode   = NAME_$LOCK_MODE[PROC1_$CURRENT];
    saved_lock_slot   = NAME_$LOCK_SLOT[PROC1_$CURRENT];

    /* 0x00E5724A: type word is 1, and the created UID goes straight into the
     * caller's new_dir_uid (A3) - there is no local copy. */
    dir_$old_create_obj(parent_uid, handle, 1, new_dir_uid, status_ret);
    if (*status_ret == status_$ok) {        /* 0x00E57262: tst.l (A4) */
        /* 0x00E57268: type word 1; `clr.w -(SP)` at 0x00E57270 zeroes the
         * 2-byte slot holding the replace boolean, so it is false. */
        dir_$old_add_entry(parent_uid, handle, parsed_name, parsed_len,
                           1, new_dir_uid, false, add_result, status_ret);
        if (*status_ret != status_$ok) {    /* 0x00E5728E: tst.l (A4) */
            /*
             * 0x00E57292-0x00E572EA: back the new object out.  Both
             * DIR_$OLD_SET_DEFAULT_ACL calls are unconditional and both hand
             * it &UID_$NIL as the ACL UID (the immediate 0x00E1737C).
             */
            DIR_$OLD_SET_DEFAULT_ACL(new_dir_uid, &ACL_$FILE_ACL, &UID_$NIL,
                                     &unlock_status);
            DIR_$OLD_SET_DEFAULT_ACL(new_dir_uid, &ACL_$DIR_ACL, &UID_$NIL,
                                     &unlock_status);
            AST_$TRUNCATE(new_dir_uid, 0, 3, &truncate_result, &unlock_status);
            /* 0x00E572E2: *new_dir_uid = UID_$NIL */
            *new_dir_uid = UID_$NIL;
        }
    }

    /* 0x00E572EE-0x00E57326: restore the caller's per-process lock state */
    NAME_$LOCK_UID[PROC1_$CURRENT]    = saved_lock_uid;
    NAME_$LOCK_HANDLE[PROC1_$CURRENT] = saved_lock_handle;
    NAME_$LOCK_MODE[PROC1_$CURRENT]   = saved_lock_mode;
    NAME_$LOCK_SLOT[PROC1_$CURRENT]   = saved_lock_slot;

    /*
     * 0x00E5732A-0x00E5733A: unlock into a local; the unlock status replaces
     * status_ret only when the whole longword is nonzero.
     */
    NAME_$UNLOCK_DIR(&unlock_status);
    if (unlock_status != status_$ok) {
        *status_ret = unlock_status;
    }

    ACL_$EXIT_SUPER();                      /* 0x00E5733C */
}
