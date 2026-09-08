/*
 * DIR_$OLD_DROP_LINKU - Legacy drop soft link
 *
 * Original address: 0x00E57924
 * Original size: 156 bytes (0x00E57924-0x00E579BF)
 *
 * Re-derived from the disassembly (bead source-us7e).  A5 = 0x00E7FD24.
 */

#include "dir/dir_internal.h"

/*
 * DIR_$OLD_DROP_LINKU - Legacy drop soft link
 *
 * Parameters:
 *   dir_uid    - (0x08,A6) -> A2, UID of the parent directory
 *   name       - (0x0c,A6) name of the link to drop
 *   name_len   - (0x10,A6) pointer to the name length word
 *   target_uid - (0x14,A6) passed straight through to dir_$old_unlink_entry
 *                as its `result` argument (0x00E5797E)
 *   status_ret - (0x18,A6) -> A3, output: status code
 */
void DIR_$OLD_DROP_LINKU(uid_t *dir_uid, char *name, uint16_t *name_len,
                         uid_t *target_uid, status_$t *status_ret)
{
    /* link.w A6,-0x2c */
    uint8_t   parsed_name[32];      /* A6-0x20 .. A6-0x01 */
    uint16_t  parsed_len;           /* A6-0x2a */
    uint32_t  handle;               /* A6-0x28 */
    status_$t unlock_status;        /* A6-0x24 */
    int8_t    leaf_ok;

    /* 0x00E5793A-0x00E57958 */
    leaf_ok = name_$validate_leaf(name, *name_len, parsed_name, &parsed_len);
    if (leaf_ok >= 0) {
        *status_ret = status_$naming_invalid_leaf;   /* 0x00E5795A */
        return;                                      /* no ACL_$EXIT_SUPER */
    }

    /* 0x00E57962: 0x00040002 => lock_mode 4, acl_rights 2 */
    NAME_$LOCK_DIR(dir_uid, &handle, 4, 2, status_ret);
    if (*status_ret != status_$ok) {    /* 0x00E57978: tst.l (A3) */
        ACL_$EXIT_SUPER();
        return;
    }

    /* 0x00E5797C: op_type 3, result = the caller's target_uid pointer */
    dir_$old_unlink_entry(dir_uid, handle, parsed_name, parsed_len,
                          3, target_uid, status_ret);

    /*
     * 0x00E5799C-0x00E579AC: the unlock status is written into a local and
     * copied over status_ret only when status_ret's LOW WORD is still zero
     * (tst.w (0x2,A3) / bne).
     */
    NAME_$UNLOCK_DIR(&unlock_status);
    if ((int16_t)*status_ret == 0) {
        *status_ret = unlock_status;
    }

    ACL_$EXIT_SUPER();                  /* 0x00E579B0 */
}
