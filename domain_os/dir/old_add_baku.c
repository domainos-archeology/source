/*
 * DIR_$OLD_ADD_BAKU - legacy "add backup entry"
 *
 * Original address: 0x00E56E3E
 * Original size: 812 bytes
 */

#include "dir/dir_internal.h"

/*
 * Constant cells the two ACL_$RIGHTS calls (0x00E56FD2 and 0x00E5705A)
 * address with `pea (d,PC)` (PC = instruction address + 2).  Both calls pass
 * the same three cells.
 */

/* 0x00E5716C, byte 0xFF: ACL_$RIGHTS' ignore_super argument (TRUE - the
 * super-user bypass is suppressed).  `pea (0x1a0,PC)` at 0x00E56FCA and
 * `pea (0x118,PC)` at 0x00E57052. */
static const boolean dir_$old_add_baku_ignore_super_00e5716c = true;

/* 0x00E56946, longword 0x00000002: the required rights mask.
 * `pea (-0x682,PC)` at 0x00E56FC6 and `pea (-0x70a,PC)` at 0x00E5704E. */
static const uint32_t dir_$old_add_baku_rights_00e56946 = 0x00000002;

/* 0x00E5472E, word 0x0000: ACL_$RIGHTS' option flags.  This is the shared
 * literal zero word NAME_$CONST_ZERO_W.  `pea (-0x2896,PC)` at 0x00E56FC2
 * and `pea (-0x291e,PC)` at 0x00E5704A. */
static const int16_t dir_$old_add_baku_acl_opts_00e5472e = 0;

/*
 * DIR_$OLD_ADD_BAKU (0x00E56E3E)
 *
 * Renames the existing entry to "<name>.bak" and re-adds `name` pointing at
 * the caller's new object.  A5 = 0xE7FD24 (the shared NAME/DIR block).
 *
 * Frame: `link.w A6,-0x154`
 *   A6-0x150  parsed_len                  A6-0x14E  the CNAME length word
 *   A6-0x14C / A6-0x14A  dir_$old_find_entry's two out words
 *   A6-0x148  directory handle            A6-0x144  the entry pointer
 *   A6-0x140  the ".BAK" entry pointer    A6-0x13C  the unlock tail's status
 *   A6-0x138  the existing object's uid   A6-0x130  a uid ACL_$DEF_ACLDATA
 *                                                   and DIR_$OLD_GET_DEFAULT_ACL
 *                                                   fill in
 *   A6-0x128  the folded name (32 bytes)  A6-0x108  the ".bak" name (32 bytes)
 *   A6-0x0E8  the 0x90-byte attribute record
 *   A6-0x058  the 0x20-byte object-location record
 *   A6-0x038  the 44-byte ACL data block  A6-0x008  a delete result cell
 *
 * Parameters (A6+0x08..A6+0x18):
 *   dir_uid    - UID of the directory
 *   name       - the entry name
 *   name_len   - pointer to the name length
 *   backup_uid - UID of the object being installed under `name`
 *   status_ret - out: status code
 */
void DIR_$OLD_ADD_BAKU(uid_t *dir_uid, char *name, uint16_t *name_len,
                       uid_t *backup_uid, status_$t *status_ret)
{
    uint16_t parsed_len;            /* A6-0x150 */
    int16_t  cname_len;             /* A6-0x14E */
    uint16_t find_out1, find_out2;  /* A6-0x14C / A6-0x14A */
    uint32_t handle;                /* A6-0x148 */
    int32_t  entry;                 /* A6-0x144 */
    int32_t  bak_entry;             /* A6-0x140 */
    status_$t unlock_status;        /* A6-0x13C */
    uid_t    old_file_uid;          /* A6-0x138 */
    uid_t    default_acl;           /* A6-0x130 */
    uint8_t  parsed_name[32];       /* A6-0x128 */
    char     name_buf[32];          /* A6-0x108 */
    uint8_t  attr_rec[AST_ATTR_REC_SIZE];   /* A6-0x0E8 */
    file_$obj_loc_t loc_rec;        /* A6-0x058 */
    uint32_t acl_data[11];          /* A6-0x038 */
    uint8_t  delete_result[8];      /* A6-0x008 */
    int8_t   valid;
    int8_t   found;
    int8_t   bak_found;
    int16_t  bak_name_len;
    int16_t  folded_bak_len;
    int16_t  i;

    /* Step 1: 0x00E56E60-0x00E56E92.  The name is rejected when
     * name_$validate_leaf reports a non-negative result, or when the folded
     * length is above 0x1C and differs from the caller's length. */
    valid = name_$validate_leaf(name, *name_len, parsed_name, &parsed_len);
    if (valid >= 0 ||
        ((int16_t)parsed_len > 0x1c && parsed_len != *name_len)) {
        /* 0x00E56E8C.  This path leaves through 0x00E57160 - no
         * ACL_$EXIT_SUPER, because none has been entered yet. */
        *status_ret = status_$naming_invalid_leaf;
        return;
    }

    /* Step 2: 0x00E56E96-0x00E56EA4 */
    if ((int16_t)*name_len <= 0x1c) {
        bak_name_len = (int16_t)(*name_len + 4);
    } else {
        bak_name_len = 0x20;
    }

    /* 0x00E56EA6-0x00E56EB2: `moveq #0x1f,D0` + `dbf` copies THIRTY-TWO
     * bytes of the caller's name unconditionally, whatever name_len says. */
    for (i = 0; i < 32; i++) {
        name_buf[i] = name[i];
    }

    /* 0x00E56EB4-0x00E56ED0: the suffix goes at name_buf[bak_name_len - 4]
     * (`lea (0x0,A6,D2w),A0` then `(-0x10c,A0)` with the buffer at
     * A6-0x108). */
    name_buf[bak_name_len - 4] = '.';
    name_buf[bak_name_len - 3] = 'b';
    name_buf[bak_name_len - 2] = 'a';
    name_buf[bak_name_len - 1] = 'k';

    /* Step 3: 0x00E56ED0-0x00E56EE2.  `move.l #0x40000` is the lock_mode /
     * acl_rights word pair (4, 0). */
    NAME_$LOCK_DIR(dir_uid, &handle, 4, 0, status_ret);
    /* 0x00E56EE6: `tst.w (0x2,A3)` - the status' LOW WORD only. */
    if ((int16_t)*status_ret != 0) {
        ACL_$EXIT_SUPER();
        return;
    }

    /* Step 4: 0x00E56EEE-0x00E56F14 */
    found = dir_$old_find_entry(handle, parsed_name, (int16_t)parsed_len,
                                &entry, &find_out1, &find_out2);

    if (found >= 0) {
        /* Step 5: 0x00E56F18 - the entry does not exist yet. */
        NAME_$UNLOCK_DIR(status_ret);
        ACL_$EXIT_SUPER();
        if ((int16_t)*status_ret != 0) {
            return;
        }

        /* 0x00E56F2E: (acl_data, uid_out) */
        ACL_$DEF_ACLDATA(acl_data, &default_acl);

        /* 0x00E56F3E-0x00E56F4C */
        DIR_$OLD_GET_DEFAULT_ACL(dir_uid, &ACL_$FILE_ACL, &default_acl,
                                 status_ret);
        if ((int16_t)*status_ret != 0) {
            return;
        }

        /* 0x00E56F5C-0x00E56F6C.  `pea (0x202,PC)` at 0x00E56F66 resolves
         * to 0x00E5716A, the protection-type word 6. */
        FILE_$SET_PROT(backup_uid, &DIR_$PROT_TYPE_ACL, acl_data,
                       &default_acl, status_ret);
        if ((int16_t)*status_ret != 0) {
            return;
        }

        /* 0x00E56F7E-0x00E56F88 */
        DIR_$OLD_ADD_HARD_LINKU(dir_uid, name, name_len, backup_uid,
                                status_ret);
        if ((int16_t)*status_ret != 0) {
            return;
        }

        /* 0x00E56F98: leaves through 0x00E57160 - the super bracket was
         * already dropped above. */
        FILE_$FW_FILE(dir_uid, status_ret);
        return;
    }

    /* Step 6: 0x00E56FA6.  The entry exists; only object type 1 may be
     * backed up. */
    if (*(uint8_t *)(uintptr_t)(entry + 0x27) != 0x01) {
        /* 0x00E5703E */
        *status_ret = status_$naming_invalid_link_operation;
        goto unlock_tail;
    }

    /* 0x00E56FB4 */
    old_file_uid.high = *(uint32_t *)(uintptr_t)(entry + 0x28);
    old_file_uid.low = *(uint32_t *)(uintptr_t)(entry + 0x2c);

    /* 0x00E56FC0-0x00E56FDE */
    ACL_$RIGHTS(&old_file_uid,
                (boolean *)&dir_$old_add_baku_ignore_super_00e5716c,
                (uint32_t *)&dir_$old_add_baku_rights_00e56946,
                (int16_t *)&dir_$old_add_baku_acl_opts_00e5472e, status_ret);
    if (*status_ret != status_$ok) {
        goto acl_error_tail;
    }

    /* 0x00E56FE2-0x00E5700A: the same suffix arithmetic on the FOLDED name,
     * with an upper-case ".BAK". */
    if ((int16_t)parsed_len <= 0x1c) {
        folded_bak_len = (int16_t)(parsed_len + 4);
    } else {
        folded_bak_len = 0x20;
    }
    parsed_name[folded_bak_len - 4] = 0x2E;   /* '.' */
    parsed_name[folded_bak_len - 3] = 0x42;   /* 'B' */
    parsed_name[folded_bak_len - 2] = 0x41;   /* 'A' */
    parsed_name[folded_bak_len - 1] = 0x4B;   /* 'K' */

    /* 0x00E5700A-0x00E57030 */
    bak_found = dir_$old_find_entry(handle, parsed_name, folded_bak_len,
                                    &bak_entry, &find_out1, &find_out2);
    if (bak_found < 0) {
        /* 0x00E57032: the existing ".BAK" must be an object too. */
        if (*(uint8_t *)(uintptr_t)(bak_entry + 0x27) != 0x01) {
            /* 0x00E5703E - the SAME store the first type check uses. */
            *status_ret = status_$naming_invalid_link_operation;
            goto unlock_tail;
        }

        /* 0x00E57048-0x00E57066 */
        ACL_$RIGHTS((uid_t *)(uintptr_t)(bak_entry + 0x28),
                    (boolean *)&dir_$old_add_baku_ignore_super_00e5716c,
                    (uint32_t *)&dir_$old_add_baku_rights_00e56946,
                    (int16_t *)&dir_$old_add_baku_acl_opts_00e5472e,
                    status_ret);
        if (*status_ret != status_$ok) {
            goto acl_error_tail;
        }
    }

    /* 0x00E57088: `tst.l (A3)` - the WHOLE status longword this time. */
    NAME_$UNLOCK_DIR(status_ret);
    if (*status_ret != status_$ok) {
        ACL_$EXIT_SUPER();
        return;
    }

    /* 0x00E57096-0x00E570AC.  `pea (-0x2580,PC)` = 0x00E54B26 (the word 1,
     * ACL_TYPE_DIR) and `pea (-0x100e,PC)` = 0x00E56094 (the word 0x0090). */
    FILE_$GET_ATTRIBUTES(&old_file_uid, &ACL_TYPE_DIR, &DIR_$ATTR_REC_SIZE_W,
                         &loc_rec, attr_rec, status_ret);
    if ((int16_t)*status_ret != 0) {
        ACL_$EXIT_SUPER();
        return;
    }

    /* 0x00E570BE-0x00E570CE.  Both data arguments point INTO the attribute
     * record: A6-0xA0 is attr_rec+0x48 and A6-0x60 is attr_rec+0x88. */
    FILE_$SET_PROT(backup_uid, &DIR_$PROT_TYPE_ACL,
                   &attr_rec[0x48], (uid_t *)(void *)&attr_rec[0x88],
                   status_ret);
    if ((int16_t)*status_ret != 0) {
        ACL_$EXIT_SUPER();
        return;
    }

    /* 0x00E570DE: `tst.b D3b` / `bpl` - D3 holds the ".BAK" find result. */
    if (bak_found < 0) {
        /* 0x00E570E2-0x00E570F6: `st`, `st`, `clr.w` - the two booleans are
         * TRUE and the third word is 0. */
        NAME_$OLD_DELETE_ENTRYU(dir_uid, name_buf, (uint16_t)bak_name_len,
                                true, true, false, delete_result, status_ret);
        /* 0x00E570FE: `tst.l (A3)` */
        if (*status_ret != status_$ok) {
            ACL_$EXIT_SUPER();
            return;
        }
    }

    /* 0x00E57102-0x00E57116: the ".bak" length is copied into its own word
     * cell before being passed by reference. */
    cname_len = bak_name_len;
    DIR_$OLD_CNAMEU(dir_uid, name, name_len,
                    name_buf, (uint16_t *)&cname_len, status_ret);
    if (*status_ret != status_$ok) {
        ACL_$EXIT_SUPER();
        return;
    }

    /* 0x00E57122-0x00E5712C */
    DIR_$OLD_ADD_HARD_LINKU(dir_uid, name, name_len, backup_uid, status_ret);
    if (*status_ret != status_$ok) {
        ACL_$EXIT_SUPER();
        return;
    }

    /* 0x00E57138 */
    FILE_$FW_FILE(dir_uid, status_ret);
    ACL_$EXIT_SUPER();
    return;

acl_error_tail:
    /* 0x00E57068-0x00E57084: both ACL_$RIGHTS failures share this tail.
     * "wrong type - operation illegal on system objects" becomes
     * "name is not a file"; anything else is converted. */
    if (*status_ret == status_$acl_wrong_type) {
        *status_ret = status_$naming_name_is_not_a_file;
    } else {
        NAME_CONVERT_ACL_STATUS(status_ret);
    }
    /* falls through */

unlock_tail:
    /* 0x00E57146-0x00E57156: the unlock reports into its OWN status cell,
     * which is copied over the caller's only when the caller's status low
     * word is zero. */
    NAME_$UNLOCK_DIR(&unlock_status);
    if ((int16_t)*status_ret == 0) {
        *status_ret = unlock_status;
    }
    ACL_$EXIT_SUPER();
}
