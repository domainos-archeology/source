/*
 * DIR_$OLD_READ_LINKU - Legacy read symbolic link
 *
 * Original address: 0x00E577F4
 * Original size: 304 bytes (0x00E577F4-0x00E57923)
 *
 * Re-derived from the disassembly (bead source-wghx).  A5 = 0x00E7FD24.
 */

#include "dir/dir_internal.h"

/*
 * DIR_$OLD_READ_LINKU - Legacy read symbolic link
 *
 * Seven longword parameters, all pointers:
 *   dir_uid    - (0x08,A6) UID of the directory to search
 *   name       - (0x0c,A6) name of the entry
 *   name_len   - (0x10,A6) pointer to the name length word
 *   target     - (0x14,A6) caller's buffer for the link text
 *   target_len - (0x18,A6) pointer to the target length word; UNMAP_CASE
 *                writes the produced length straight into it (0x00E578D0)
 *   target_uid - (0x1c,A6) -> A4, output: UID for a type-1 entry
 *   status_ret - (0x20,A6) -> A3, output: status code
 */
void DIR_$OLD_READ_LINKU(uid_t *dir_uid, char *name, uint16_t *name_len,
                         void *target, uint16_t *target_len,
                         uid_t *target_uid, status_$t *status_ret)
{
    /* link.w A6,-0x138 */
    int8_t    truncated;            /* A6-0x136 */
    uint16_t  parsed_len;           /* A6-0x134 */
    uint16_t  link_len;             /* A6-0x132 */
    uint16_t  slot_idx;             /* A6-0x130 */
    uint16_t  chain_level;          /* A6-0x12e */
    uint32_t  handle;               /* A6-0x12c */
    int32_t   entry_ptr;            /* A6-0x128 */
    status_$t unlock_status;        /* A6-0x124 */
    uint8_t   parsed_name[32];      /* A6-0x120 .. A6-0x101 */
    char      link_buf[256];        /* A6-0x100 .. A6-0x01 */
    char     *entry;                /* A2 */
    uint16_t  entry_type;
    int8_t    leaf_ok;
    int8_t    found;

    /* 0x00E57806-0x00E57812 */
    *target_uid = UID_$NIL;

    /* 0x00E57816-0x00E57834 */
    leaf_ok = name_$validate_leaf(name, *name_len, parsed_name, &parsed_len);
    if (leaf_ok >= 0) {
        *status_ret = status_$naming_invalid_leaf;   /* 0x00E57836 */
        return;                                      /* no ACL_$EXIT_SUPER */
    }

    /* 0x00E57840: 0x00010004 => lock_mode 1, acl_rights 4 */
    NAME_$LOCK_DIR(dir_uid, &handle, 1, 4, status_ret);
    if ((int16_t)*status_ret != 0) {    /* 0x00E57858: tst.w (2,A3) */
        ACL_$EXIT_SUPER();              /* 0x00E57914 */
        return;
    }

    /* 0x00E57860 */
    found = dir_$old_find_entry(handle, parsed_name, parsed_len,
                                &entry_ptr, &slot_idx, &chain_level);
    /* 0x00E57882: `movea.l (-0x128,A6),A2` - a 32-bit target address. */
    entry = (char *)ARCH_VA_TO_PTR(entry_ptr);
    if (found >= 0) {
        *status_ret = status_$naming_name_not_found;    /* 0x00E578FA */
    } else {
        /* 0x00E5788A: zero-extended entry type byte at entry+0x27 */
        entry_type = *(uint8_t *)(entry + 0x27);

        if (entry_type == 1) {
            /* 0x00E578A2: a hard entry names an object, not a link */
            target_uid->high = *(uint32_t *)(entry + 0x28);
            target_uid->low  = *(uint32_t *)(entry + 0x2c);
            *status_ret = status_$naming_not_a_link;    /* 0x000E0006 */
        } else if (entry_type == 3) {
            /* 0x00E578B4: pull the link text out of the overflow blocks */
            dir_$old_read_link_data(handle, entry + 0x28,
                                    (uint8_t *)link_buf, &link_len);

            /*
             * 0x00E578CC: un-map the case straight into the caller's buffer.
             * The max_out_len VAR argument is the shared word at 0x00E577F2
             * (`pea (-0xe4,PC)`) and the produced length is written into the
             * caller's target_len.
             */
            UNMAP_CASE(link_buf, (int16_t *)&link_len, (char *)target,
                       &DIR_$OLD_LINK_TEXT_MAX, (int16_t *)target_len,
                       (uint8_t *)&truncated);
            if (truncated < 0) {                        /* 0x00E578EC: bpl */
                *status_ret = status_$naming_invalid_link;
            }
        }
        /* entry_type 0 and everything else: 0x00E578A0 falls to the tail */
    }

    /*
     * 0x00E57900-0x00E57910: unlock into a local; it replaces status_ret only
     * when status_ret's low word is still zero.
     */
    NAME_$UNLOCK_DIR(&unlock_status);
    if ((int16_t)*status_ret == 0) {
        *status_ret = unlock_status;
    }

    ACL_$EXIT_SUPER();                  /* 0x00E57914 */
}
