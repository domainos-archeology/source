/*
 * NAME ASID (Address Space ID) Management Functions
 *
 * Functions to initialize, copy (fork), and free naming state
 * for address spaces (processes).
 *
 * Each ASID has its own:
 *   - Working directory UID and mapped info (16 bytes at 0x950 + ASID*8 / 0x5B0 + ASID*16)
 *   - Naming directory UID and mapped info (16 bytes at 0x3E0 + ASID*8 / 0x040 + ASID*16)
 *
 * Original addresses:
 *   NAME_$INIT_ASID: 0x00e73cfc (318 bytes)
 *   NAME_$FORK:      0x00e73e44 (186 bytes)
 *   NAME_$FREE_ASID: 0x00e74da8 (130 bytes)
 */

#include "name/name_internal.h"

/* name_$unmap_dir_buffers declared in name/name_internal.h */

/*
 * Per-ASID data lives in NAME_$DATA (0xE80264), see name/name.h:
 *   ndir_uid[]         at +0x3E0 (8 bytes per ASID)
 *   wdir_uid[]         at +0x950 (8 bytes per ASID)
 *   ndir_mapped_info[] at +0x040 (16 bytes per ASID)
 *   wdir_mapped_info[] at +0x5B0 (16 bytes per ASID)
 */

/*
 * ============================================================================
 * Constant cells
 * ============================================================================
 *
 * The compiler pooled these three literals just past NAME_$INIT_ASID; both
 * of its ACL_$RIGHTS calls address them with `pea (d,PC)` (PC = instruction
 * address + 2) and both pass the same three cells.
 */

/* 0x00E73E3C, byte 0xFF: ACL_$RIGHTS' ignore_super argument (TRUE - the
 * super-user bypass is suppressed even though NAME_$INIT_ASID has just
 * called ACL_$ENTER_SUPER).  `pea (0x100,PC)` at 0x00E73D3A and
 * `pea (0x74,PC)` at 0x00E73DC6. */
static const boolean name_$init_asid_ignore_super_00e73e3c = true;

/* 0x00E73E40, longword 0xFFFFFFFF: the required rights mask.
 * `pea (0x108,PC)` at 0x00E73D36 and `pea (0x7c,PC)` at 0x00E73DC2. */
static const uint32_t name_$init_asid_rights_00e73e40 = 0xFFFFFFFFu;

/* 0x00E73E3A, word 0x0001: ACL_$RIGHTS' option flags (object type 1,
 * directory).  `pea (0x106,PC)` at 0x00E73D32 and `pea (0x7a,PC)` at
 * 0x00E73DBE. */
static const int16_t name_$init_asid_acl_opts_00e73e3a = 1;

/*
 * NAME_$INIT_ASID - Initialize naming state for a new address space
 *
 * Called when creating a new process. Copies the current process's
 * working and naming directories to the new ASID, checking ACL access.
 *
 * Parameters:
 *   new_asid   - Pointer to the new address space ID
 *   status_ret - Output: status code
 *
 * Original address: 0x00e73cfc
 */
void NAME_$INIT_ASID(int16_t *new_asid, status_$t *status_ret)
{
    uid_t current_uid;
    uid_t *src_wdir = &NAME_$DATA.wdir_uid[PROC1_$AS_ID];
    uid_t *dst_wdir = &NAME_$DATA.wdir_uid[*new_asid];
    uid_t *src_ndir = &NAME_$DATA.ndir_uid[PROC1_$AS_ID];
    uid_t *dst_ndir = &NAME_$DATA.ndir_uid[*new_asid];

    ACL_$ENTER_SUPER();

    /* Copy and map working directory */
    current_uid.high = src_wdir->high;
    current_uid.low = src_wdir->low;

    /* Check ACL access for working directory.
     * 0x00E73D4C `tst.l D0` + `sne` + `bpl`: the whole longword result. */
    if (ACL_$RIGHTS(&current_uid,
                    (boolean *)&name_$init_asid_ignore_super_00e73e3c,
                    (uint32_t *)&name_$init_asid_rights_00e73e40,
                    (int16_t *)&name_$init_asid_acl_opts_00e73e3a,
                    status_ret) != 0) {
        /* Has access - map the directory for the new ASID */
        name_$map_dir(&current_uid, *new_asid,
                     &NAME_$DATA.wdir_mapped_info[*new_asid],
                     status_ret);

        if (*status_ret == status_$ok) {
            dst_wdir->high = current_uid.high;
            dst_wdir->low = current_uid.low;
            goto do_ndir;
        }
    } else {
        *status_ret = status_$ok;
do_ndir:
        /* Copy and map naming directory */
        current_uid.high = src_ndir->high;
        current_uid.low = src_ndir->low;

        /* Check ACL access for naming directory.
         * 0x00E73DD8 `tst.l D0` + `sne` + `bpl`. */
        if (ACL_$RIGHTS(&current_uid,
                        (boolean *)&name_$init_asid_ignore_super_00e73e3c,
                        (uint32_t *)&name_$init_asid_rights_00e73e40,
                        (int16_t *)&name_$init_asid_acl_opts_00e73e3a,
                        status_ret) != 0) {
            name_$map_dir(&current_uid, *new_asid,
                         &NAME_$DATA.ndir_mapped_info[*new_asid],
                         status_ret);

            if (*status_ret == status_$ok) {
                dst_ndir->high = current_uid.high;
                dst_ndir->low = current_uid.low;
                goto done;
            }
        } else {
            *status_ret = status_$ok;
            goto done;
        }
    }

    /* Set high bit to indicate error */
    *status_ret |= 0x80000000;  /* high bit of the first byte (m68k big-endian) */

done:
    ACL_$EXIT_SUPER();
}

/*
 * NAME_$FORK - Copy naming state from parent to child during fork
 *
 * Copies the working directory, naming directory, and their mapped info
 * structures from the parent ASID to the child ASID.
 *
 * Parameters:
 *   parent_asid - Pointer to parent address space ID
 *   child_asid  - Pointer to child address space ID
 *
 * Original address: 0x00e73e44
 */
void NAME_$FORK(int16_t *parent_asid, int16_t *child_asid)
{
    uid_t *parent_wdir = &NAME_$DATA.wdir_uid[*parent_asid];
    uid_t *child_wdir = &NAME_$DATA.wdir_uid[*child_asid];
    uid_t *parent_ndir = &NAME_$DATA.ndir_uid[*parent_asid];
    uid_t *child_ndir = &NAME_$DATA.ndir_uid[*child_asid];

    /* Copy working directory UID */
    child_wdir->high = parent_wdir->high;
    child_wdir->low = parent_wdir->low;

    /* Copy naming directory UID */
    child_ndir->high = parent_ndir->high;
    child_ndir->low = parent_ndir->low;

    /* Copy working directory mapped info (16 bytes) */
    NAME_$DATA.wdir_mapped_info[*child_asid] = NAME_$DATA.wdir_mapped_info[*parent_asid];

    /* Copy naming directory mapped info (16 bytes) */
    NAME_$DATA.ndir_mapped_info[*child_asid] = NAME_$DATA.ndir_mapped_info[*parent_asid];

    /* The decompiled code shows it copies twice - this appears to be
     * for redundancy or there may be two separate mapped info structures.
     * Replicating the behavior here. */
    NAME_$DATA.wdir_mapped_info[*child_asid] = NAME_$DATA.wdir_mapped_info[*parent_asid];

    NAME_$DATA.ndir_mapped_info[*child_asid] = NAME_$DATA.ndir_mapped_info[*parent_asid];
}

/*
 * NAME_$FREE_ASID - Free naming state for an address space
 *
 * Called when a process terminates. Unmaps the directories and
 * resets the UIDs to the node directory UID.
 *
 * Parameters:
 *   asid - Pointer to the address space ID to free
 *
 * Original address: 0x00e74da8
 */
void NAME_$FREE_ASID(int16_t *asid)
{
    uid_t *wdir = &NAME_$DATA.wdir_uid[*asid];
    uid_t *ndir = &NAME_$DATA.ndir_uid[*asid];

    ACL_$ENTER_SUPER();

    /* Unmap working directory */
    name_$unmap_dir_buffers(*asid, &NAME_$DATA.wdir_mapped_info[*asid]);

    /* Unmap naming directory */
    name_$unmap_dir_buffers(*asid, &NAME_$DATA.ndir_mapped_info[*asid]);

    /* Reset both UIDs to node directory */
    wdir->high = NAME_$NODE_UID.high;
    wdir->low = NAME_$NODE_UID.low;
    ndir->high = NAME_$NODE_UID.high;
    ndir->low = NAME_$NODE_UID.low;

    ACL_$EXIT_SUPER();
}
