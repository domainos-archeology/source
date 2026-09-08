/*
 * DIR_$OLD_CNAMEU - Legacy change name (rename) entry
 *
 * Original address: 0x00E57562
 * Original size: 392 bytes (0x00E57562-0x00E576E9)
 *
 * Re-derived from the disassembly (bead source-rv9v).  A5 = 0x00E7FD24.
 */

#include "dir/dir_internal.h"

/*
 * DIR_$OLD_CNAMEU - Legacy change name (rename) entry
 *
 * Parameters:
 *   dir_uid      - (0x08,A6) -> D2, UID of the directory holding the entry
 *   old_name     - (0x0c,A6) current name
 *   old_name_len - (0x10,A6) pointer to the current name length word
 *   new_name     - (0x14,A6) new name
 *   new_name_len - (0x18,A6) pointer to the new name length word
 *   status_ret   - (0x1c,A6) -> A3, output: status code
 *
 * The image validates the NEW name first (0x00E57578, from (0x14,A6) /
 * (0x18,A6)) and the OLD name second (0x00E57598); either failure yields
 * status_$naming_invalid_leaf.
 */
void DIR_$OLD_CNAMEU(uid_t *dir_uid, char *old_name, uint16_t *old_name_len,
                     char *new_name, uint16_t *new_name_len,
                     status_$t *status_ret)
{
    /* link.w A6,-0x5c */
    uint16_t  slot_idx;             /* A6-0x5a */
    uint16_t  chain_level;          /* A6-0x58 */
    uint16_t  new_parsed_len;       /* A6-0x56 */
    uint16_t  hash;                 /* A6-0x54 */
    uint16_t  old_parsed_len;       /* A6-0x52 */
    int32_t   entry_ptr;            /* A6-0x50 */
    uint8_t   add_result[4];        /* A6-0x4c */
    uint32_t  handle;               /* A6-0x48 */
    status_$t unlock_status;        /* A6-0x44 */
    uint8_t   new_parsed[32];       /* A6-0x40 .. A6-0x21 */
    uint8_t   old_parsed[32];       /* A6-0x20 .. A6-0x01 */
    uint32_t  dir_base;             /* A4 - the mapped directory base */
    char     *entry;                /* A2 */
    uint16_t  entry_type;
    int8_t    leaf_ok;
    int8_t    found;

    /* 0x00E57578: the NEW name is validated first */
    leaf_ok = name_$validate_leaf(new_name, *new_name_len,
                                  new_parsed, &new_parsed_len);
    if (leaf_ok < 0) {
        /* 0x00E57598: then the OLD name */
        leaf_ok = name_$validate_leaf(old_name, *old_name_len,
                                      old_parsed, &old_parsed_len);
    }
    if (leaf_ok >= 0) {
        *status_ret = status_$naming_invalid_leaf;   /* 0x00E575B8 */
        return;                                      /* no ACL_$EXIT_SUPER */
    }

    /* 0x00E575C2: 0x00040002 => lock_mode 4, acl_rights 2 */
    NAME_$LOCK_DIR(dir_uid, &handle, 4, 2, status_ret);
    dir_base = handle;                              /* 0x00E575D8: A4 */
    if ((int16_t)*status_ret != 0) {                /* 0x00E575DC: tst.w (2,A3) */
        ACL_$EXIT_SUPER();                          /* 0x00E576DA */
        return;
    }

    /* 0x00E575E4: locate the entry under its OLD name */
    found = dir_$old_find_entry(dir_base, old_parsed, old_parsed_len,
                                &entry_ptr, &slot_idx, &chain_level);
    /* 0x00E57604: `movea.l (-0x50,A6),A2` - a 32-bit target address. */
    entry = (char *)ARCH_VA_TO_PTR(entry_ptr);
    if (found >= 0) {
        *status_ret = status_$naming_name_not_found;    /* 0x00E5760C */
    } else {
        /* 0x00E57638 / 0x00E5766E: the entry type byte at entry+0x27 */
        entry_type = *(uint8_t *)(entry + 0x27);

        /* 0x00E57616-0x00E57626: 8-byte compare against NAME_$ROOT_UID */
        if (dir_uid->high == NAME_$ROOT_UID.high &&
            dir_uid->low  == NAME_$ROOT_UID.low) {
            /*
             * Root directory.  Bit 7 of the entry's byte at +0x24 is set
             * across the add and cleared again afterwards (0x00E57654 /
             * 0x00E57688), and the extra longword is the entry's own value
             * at +0x20 rather than zero.
             */
            *(uint8_t *)(entry + 0x24) |= 0x80;
            dir_$old_add_entry_ext(dir_uid, dir_base, new_parsed,
                                   new_parsed_len, entry_type,
                                   entry + 0x28,
                                   *(uint32_t *)(entry + 0x20),
                                   0xFF, add_result, status_ret);
            *(uint8_t *)(entry + 0x24) &= (uint8_t)~0x80u;
        } else {
            /* 0x00E57628: non-root, flags word 0 */
            dir_$old_add_entry(dir_uid, dir_base, new_parsed, new_parsed_len,
                               entry_type, entry + 0x28,
                               0, add_result, status_ret);
        }

        if ((int16_t)*status_ret == 0) {    /* 0x00E5768E: tst.w (2,A3) */
            /* 0x00E57694: the old slot is marked type 1 before it is freed */
            *(uint8_t *)(entry + 0x27) = 1;

            /*
             * 0x00E5769A: the hash is taken over the OLD name, with the
             * bucket count read from the directory header word at +0x02.
             */
            hash = dir_$old_hash_name(
                       old_parsed, old_parsed_len,
                       *(uint16_t *)((uint8_t *)NAME_$HANDLE_TO_PTR(dir_base)
                                     + 2));
            dir_$old_delete_entry(dir_base, slot_idx, chain_level, hash);
        }
    }

    /*
     * 0x00E576C6-0x00E576D6: unlock into a local; it replaces status_ret only
     * when status_ret's low word is still zero.
     */
    NAME_$UNLOCK_DIR(&unlock_status);
    if ((int16_t)*status_ret == 0) {
        *status_ret = unlock_status;
    }

    ACL_$EXIT_SUPER();                      /* 0x00E576DA */
}
