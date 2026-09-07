/*
 * name_$old_add_link_local - the local half of name_$old_add_link
 *
 * Original address: 0x00E565B8 (was FUN_00e565b8), 202 bytes.
 * Sole caller: name_$old_add_link (0x00E56910), which passes acl_rights = 0.
 *
 * Frame (A6+), fixed by the register loads at 0x00E565C0-0x00E565CC and by the
 * caller's pushes at 0x00E56900-0x00E5690E:
 *   0x08 dir_uid    (long) -> A4
 *   0x0C acl_rights (word)        NAME_$LOCK_DIR's fourth parameter
 *   0x0E name       (long) -> A3
 *   0x12 name_len   (word)
 *   0x14 file_uid   (long) -> D2
 *   0x18 status_ret (long) -> A2
 *
 * Locals (A6-):
 *   -0x36 leaf_len       word   name_$validate_leaf's parsed length
 *   -0x34 handle         long   NAME_$LOCK_DIR's mapped directory base
 *   -0x30 unlock_status  long   NAME_$UNLOCK_DIR's own status
 *   -0x2C add_result     long   dir_$old_add_entry's new-entry pointer
 *   -0x28 leaf           0x20   the parsed leaf name
 *
 * Adding to the root directory is a different operation entirely, so it is
 * forwarded whole to name_$old_add_entry; everything else is done here under a
 * directory lock.  Note that the forwarding arm branches PAST the
 * ACL_$EXIT_SUPER at 0x00E56672 (`bra.b 0x00e56678` at 0x00E565F8) - only the
 * locked path exits super mode, because only NAME_$LOCK_DIR entered it.
 */

#include "name/name_internal.h"
#include "dir/dir.h"
#include "acl/acl.h"

/* NAME_$LOCK_DIR lock mode 4 (`move.w #0x4,-(SP)` at 0x00E56624) - the same
 * mode name_$old_drop_entry and the other mutating entry points use. */
#define NAME_LOCK_MODE_ADD_ENTRY    4

/* dir_$old_add_entry's `type` selector (`move.w #0x1,-(SP)` at 0x00E56646):
 * 1 = a plain object entry. */
#define DIR_ENTRY_TYPE_OBJECT       1

void name_$old_add_link_local(uid_t *dir_uid, int16_t acl_rights, char *name,
                              uint16_t name_len, uid_t *file_uid,
                              status_$t *status_ret)
{
    uint8_t   leaf[0x20];           /* A6-0x28 */
    uint32_t  handle;               /* A6-0x34 */
    uint32_t  add_result;           /* A6-0x2C */
    status_$t unlock_status;        /* A6-0x30 */
    uint16_t  leaf_len;             /* A6-0x36 */

    /*
     * 0x00E565D0-0x00E565F8: the root directory is not a mapped directory, so
     * hand the whole request to name_$old_add_entry and return.  The zero
     * longword pushed at 0x00E565E4 is that routine's `flags` argument.
     */
    if (dir_uid->high == NAME_$ROOT_UID.high &&
        dir_uid->low  == NAME_$ROOT_UID.low) {
        name_$old_add_entry(dir_uid, (uint16_t)acl_rights, name, name_len,
                            file_uid, 0, status_ret);
        return;
    }

    /*
     * 0x00E565FA-0x00E5661C.  name_$validate_leaf returns a Domain boolean:
     * negative means the name is a usable leaf.
     */
    if (name_$validate_leaf(name, name_len, leaf, &leaf_len) >= 0) {
        *status_ret = status_$naming_invalid_leaf;      /* 0x000E000B */
        return;
    }

    /* 0x00E5661E-0x00E56638 */
    NAME_$LOCK_DIR(dir_uid, &handle, NAME_LOCK_MODE_ADD_ENTRY, acl_rights,
                   status_ret);
    if (*status_ret != status_$ok) {
        goto exit_super;                                /* 0x00E56638 */
    }

    /* 0x00E5663A-0x00E5665C */
    dir_$old_add_entry(dir_uid, handle, leaf, leaf_len, DIR_ENTRY_TYPE_OBJECT,
                       file_uid, 0, (uint8_t *)&add_result, status_ret);

    /*
     * 0x00E56660-0x00E56670: the unlock reports into its OWN cell and only
     * overwrites the caller's status when it is non-zero, so a successful
     * unlock cannot mask an add failure.
     */
    NAME_$UNLOCK_DIR(&unlock_status);
    if (unlock_status != status_$ok) {
        *status_ret = unlock_status;
    }

exit_super:                                             /* 0x00E56672 */
    ACL_$EXIT_SUPER();
}
