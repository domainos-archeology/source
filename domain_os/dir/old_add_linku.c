/*
 * DIR_$OLD_ADD_LINKU - Legacy add soft/symbolic link
 *
 * Original address: 0x00E576EA
 * Original size: 264 bytes (0x00E576EA-0x00E577F1)
 *
 * Re-derived from the disassembly (bead source-vy6m).  A5 = 0x00E7FD24.
 */

#include "dir/dir_internal.h"

/*
 * DIR_$OLD_ADD_LINKU - Legacy add soft/symbolic link
 *
 * Parameters:
 *   dir_uid    - (0x08,A6) -> A2, UID of the directory to hold the link
 *   name       - (0x0c,A6) name for the link
 *   name_len   - (0x10,A6) pointer to the name length word
 *   target     - (0x14,A6) target pathname text
 *   target_len - (0x18,A6) pointer to the target length word
 *   status_ret - (0x1c,A6) -> A3, output: status code
 *
 * The routine does NOT reject links in the root directory.  0x00E57776 only
 * compares dir_uid against NAME_$ROOT_UID and `seq`s the answer into a
 * Domain boolean at A6-0x136, which becomes the seventh argument of
 * dir_$old_add_link_entry at 0x00E577AE.
 */
void DIR_$OLD_ADD_LINKU(uid_t *dir_uid, char *name, int16_t *name_len,
                        void *target, uint16_t *target_len,
                        status_$t *status_ret)
{
    /* link.w A6,-0x138 */
    int8_t            truncated;        /* A6-0x138 */
    uint8_t           is_root;          /* A6-0x136 - Domain boolean, 0xFF/0x00 */
    int16_t           mapped_len;       /* A6-0x134 */
    start_path_type_t path_type;        /* A6-0x132 */
    int16_t           consumed;         /* A6-0x130 */
    uint16_t          parsed_len;       /* A6-0x12e */
    uint32_t          handle;           /* A6-0x12c */
    status_$t         unlock_status;    /* A6-0x128 */
    uint8_t           add_result[4];    /* A6-0x124 */
    char              mapped_target[256];   /* A6-0x120 .. A6-0x21 */
    uint8_t           parsed_name[32];      /* A6-0x20 .. A6-0x01 */
    int8_t            leaf_ok;
    boolean           path_ok;

    /* 0x00E57700-0x00E5771E */
    leaf_ok = name_$validate_leaf(name, (uint16_t)*name_len,
                                  parsed_name, &parsed_len);
    if (leaf_ok >= 0) {
        *status_ret = status_$naming_invalid_leaf;   /* 0x00E57720 */
        return;                                      /* no ACL_$EXIT_SUPER */
    }

    /*
     * 0x00E5772A: case-map the target text.  The max_out_len VAR argument is
     * the shared word at 0x00E577F2 (`pea (0xbe,PC)`), the size of the
     * mapped_target buffer.
     */
    MAP_CASE((char *)target, (int16_t *)target_len, mapped_target,
             &DIR_$OLD_LINK_TEXT_MAX, &mapped_len, (uint8_t *)&truncated);
    if (truncated < 0) {                            /* 0x00E5774A: bmi */
        *status_ret = status_$naming_invalid_link;  /* 0x00E5776E */
        return;
    }

    /* 0x00E57750 */
    path_ok = NAME_$VALIDATE(mapped_target, (uint16_t *)&mapped_len,
                             &consumed, &path_type);
    if ((int8_t)path_ok >= 0) {                     /* 0x00E5776A: bmi */
        *status_ret = status_$naming_invalid_link;  /* 0x00E5776E */
        return;
    }

    /*
     * 0x00E57776-0x00E57788: `seq` the 8-byte compare against NAME_$ROOT_UID
     * into a Domain boolean.  Nothing branches on it here.
     */
    is_root = (dir_uid->high == NAME_$ROOT_UID.high &&
               dir_uid->low  == NAME_$ROOT_UID.low) ? (uint8_t)0xFF : (uint8_t)0x00;

    /* 0x00E5778C: 0x00040002 => lock_mode 4, acl_rights 2 */
    NAME_$LOCK_DIR(dir_uid, &handle, 4, 2, status_ret);
    if (*status_ret != status_$ok) {    /* 0x00E577A2: tst.l (A3) */
        ACL_$EXIT_SUPER();              /* 0x00E577E2 */
        return;
    }

    /* 0x00E577A6: is_root is the seventh argument */
    dir_$old_add_link_entry(dir_uid, handle, parsed_name, parsed_len,
                            mapped_target, (uint16_t)mapped_len, is_root,
                            add_result, status_ret);

    /*
     * 0x00E577D0-0x00E577E0: the unlock status replaces status_ret whenever
     * the whole longword is nonzero.
     */
    NAME_$UNLOCK_DIR(&unlock_status);
    if (unlock_status != status_$ok) {
        *status_ret = unlock_status;
    }

    ACL_$EXIT_SUPER();                  /* 0x00E577E2 */
}
