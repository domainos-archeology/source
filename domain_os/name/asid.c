/*
 * NAME ASID (Address Space ID) management
 *
 * Initialise, copy (fork) and free the per-address-space naming state held
 * in NAME_$DATA (0xE80264):
 *   ndir_mapped_info[]  +0x040, 16 bytes per ASID
 *   ndir_uid[]          +0x3E0,  8 bytes per ASID
 *   wdir_mapped_info[]  +0x5B0, 16 bytes per ASID
 *   wdir_uid[]          +0x950,  8 bytes per ASID
 *   node_uid            +0x030
 *
 * SAU2 map:
 *   NAME segment at E73CFC (size 0x204): NAME_$INIT_ASID E73CFC, NAME_$FORK E73E44
 *   NAME segment at E74DA8 (size 0x84):  NAME_$FREE_ASID E74DA8
 */

#include "name/name_internal.h"

/*
 * ============================================================================
 * Constant cells
 * ============================================================================
 *
 * The compiler pooled these three literals just past NAME_$INIT_ASID; both
 * of its ACL_$RIGHTS calls address them with `pea (d,PC)` (PC = instruction
 * address + 2) and both pass the same three cells.  Image bytes at 0xE73E3A:
 * `00 01 ff 00 20 48 ff ff ff ff`.
 */

/* 0x00E73E3A, word 0x0001: ACL_$RIGHTS' option flags (object type 1,
 * directory).  `pea (0x106,PC)` at 0x00E73D32 and `pea (0x7a,PC)` at
 * 0x00E73DBE. */
static const int16_t name_$init_asid_acl_opts_00e73e3a = 1;

/* 0x00E73E3C, byte 0xFF: ACL_$RIGHTS' ignore_super argument (TRUE - the
 * super-user bypass is suppressed even though NAME_$INIT_ASID has just
 * called ACL_$ENTER_SUPER).  `pea (0x100,PC)` at 0x00E73D3A and
 * `pea (0x74,PC)` at 0x00E73DC6. */
static const boolean name_$init_asid_ignore_super_00e73e3c = true;

/* 0x00E73E40, longword 0xFFFFFFFF: the required rights mask.
 * `pea (0x108,PC)` at 0x00E73D36 and `pea (0x7c,PC)` at 0x00E73DC2. */
static const uint32_t name_$init_asid_rights_00e73e40 = 0xFFFFFFFFu;

/*
 * NAME_$INIT_ASID (0x00E73CFC, 318 bytes)
 *
 * Give a new address space the current one's working and naming
 * directories.  For each of the two: copy the current ASID's UID into a
 * local, ask ACL_$RIGHTS about it, and if any right comes back map it for
 * the new ASID and store the UID; no rights at all is silently status_$ok.
 * A map failure sets bit 31 of the status and skips the rest.
 *
 * Frame: (0x8,A6) new_asid -> A2, (0xc,A6) status_ret -> A3; the UID copy
 * lives at A6-0x8.
 */
void NAME_$INIT_ASID(int16_t *new_asid, status_$t *status_ret)
{
    uid_t   current_uid;                    /* A6-0x8 */
    uid_t  *src;
    uid_t  *dst;

    ACL_$ENTER_SUPER();                                     /* 0x00E73D0C */

    /* 0x00E73D12-0x00E73D2C: current_uid = wdir_uid[PROC1_$AS_ID] */
    src = &NAME_$DATA.wdir_uid[PROC1_$AS_ID];
    current_uid.high = src->high;
    current_uid.low  = src->low;

    /* 0x00E73D30-0x00E73D52: `tst.l D0 / sne / tst.b / bpl` - any right. */
    if (ACL_$RIGHTS(&current_uid,
                    (boolean *)&name_$init_asid_ignore_super_00e73e3c,
                    (uint32_t *)&name_$init_asid_rights_00e73e40,
                    (int16_t *)&name_$init_asid_acl_opts_00e73e3a,
                    status_ret) != 0) {
        /* 0x00E73D54-0x00E73D76 */
        name_$map_dir(&current_uid, *new_asid,
                      &NAME_$DATA.wdir_mapped_info[*new_asid],
                      status_ret);
        if (*status_ret != status_$ok) {                    /* 0x00E73D7A */
            goto map_failed;                                /* bne.w 0x00E73E06 */
        }
        /* 0x00E73D80-0x00E73D96: wdir_uid[*new_asid] = current_uid */
        dst = &NAME_$DATA.wdir_uid[*new_asid];
        dst->high = current_uid.high;
        dst->low  = current_uid.low;
    } else {
        *status_ret = status_$ok;                           /* 0x00E73D9C */
    }

    /* 0x00E73D9E-0x00E73DB8: current_uid = ndir_uid[PROC1_$AS_ID] */
    src = &NAME_$DATA.ndir_uid[PROC1_$AS_ID];
    current_uid.high = src->high;
    current_uid.low  = src->low;

    /* 0x00E73DBC-0x00E73DDE */
    if (ACL_$RIGHTS(&current_uid,
                    (boolean *)&name_$init_asid_ignore_super_00e73e3c,
                    (uint32_t *)&name_$init_asid_rights_00e73e40,
                    (int16_t *)&name_$init_asid_acl_opts_00e73e3a,
                    status_ret) != 0) {
        /* 0x00E73DE0-0x00E73DFE */
        name_$map_dir(&current_uid, *new_asid,
                      &NAME_$DATA.ndir_mapped_info[*new_asid],
                      status_ret);
        if (*status_ret != status_$ok) {                    /* 0x00E73E02 */
            goto map_failed;
        }
        /* 0x00E73E0C-0x00E73E22: ndir_uid[*new_asid] = current_uid */
        dst = &NAME_$DATA.ndir_uid[*new_asid];
        dst->high = current_uid.high;
        dst->low  = current_uid.low;
    } else {
        *status_ret = status_$ok;                           /* 0x00E73E28 */
    }
    goto done;

map_failed:
    /* 0x00E73E06 `bset.b #0x7,(A3)`: bit 7 of the first byte = bit 31. */
    *status_ret = (status_$t)((uint32_t)*status_ret | 0x80000000u);

done:
    ACL_$EXIT_SUPER();                                      /* 0x00E73E2A */
}

/*
 * NAME_$FORK (0x00E73E44, 186 bytes)
 *
 * Copy the parent ASID's working/naming directory UIDs and both 16-byte
 * mapped-info records to the child.  The two mapped-info copies are done
 * TWICE in the image (0x00E73EA4-0x00E73EC6 and again 0x00E73ED0-0x00E73EF2,
 * same source and destination); reproduced as found.
 *
 * Frame: (0x8,A6) parent_asid -> D2/A0, (0xc,A6) child_asid -> A2.
 */
void NAME_$FORK(int16_t *parent_asid, int16_t *child_asid)
{
    uid_t *parent_wdir;
    uid_t *child_wdir;
    uid_t *parent_ndir;
    uid_t *child_ndir;

    /* 0x00E73E54-0x00E73E82: the two UIDs, indexed by the two asid words. */
    parent_wdir = &NAME_$DATA.wdir_uid[*parent_asid];
    child_wdir  = &NAME_$DATA.wdir_uid[*child_asid];
    child_wdir->high = parent_wdir->high;
    child_wdir->low  = parent_wdir->low;

    parent_ndir = &NAME_$DATA.ndir_uid[*parent_asid];
    child_ndir  = &NAME_$DATA.ndir_uid[*child_asid];
    child_ndir->high = parent_ndir->high;
    child_ndir->low  = parent_ndir->low;

    /* 0x00E73E84-0x00E73EC6: wdir then ndir mapped info, 4 longwords each. */
    NAME_$DATA.wdir_mapped_info[*child_asid] = NAME_$DATA.wdir_mapped_info[*parent_asid];
    NAME_$DATA.ndir_mapped_info[*child_asid] = NAME_$DATA.ndir_mapped_info[*parent_asid];

    /* 0x00E73EC8-0x00E73EF2: and the same two copies again. */
    NAME_$DATA.wdir_mapped_info[*child_asid] = NAME_$DATA.wdir_mapped_info[*parent_asid];
    NAME_$DATA.ndir_mapped_info[*child_asid] = NAME_$DATA.ndir_mapped_info[*parent_asid];
}

/*
 * NAME_$FREE_ASID (0x00E74DA8, 130 bytes)
 *
 * Unmap the ASID's working and naming directory buffers and reset both
 * UIDs to this node's directory (NAME_$DATA+0x30).
 *
 * Frame: (0x8,A6) asid -> A2.
 */
void NAME_$FREE_ASID(int16_t *asid)
{
    uid_t *wdir;
    uid_t *ndir;

    ACL_$ENTER_SUPER();                                     /* 0x00E74DB4 */

    /* 0x00E74DBA-0x00E74DD6: (asid, &wdir_mapped_info[asid]) */
    name_$unmap_dir_buffers(*asid, &NAME_$DATA.wdir_mapped_info[*asid]);

    /* 0x00E74DD8-0x00E74DEC: (asid, &ndir_mapped_info[asid]) - the index
     * still in D2w from the first call. */
    name_$unmap_dir_buffers(*asid, &NAME_$DATA.ndir_mapped_info[*asid]);

    /* 0x00E74DEE-0x00E74E16: both UIDs <- node_uid (+0x30). */
    wdir = &NAME_$DATA.wdir_uid[*asid];
    ndir = &NAME_$DATA.ndir_uid[*asid];
    wdir->high = NAME_$NODE_UID.high;
    wdir->low  = NAME_$NODE_UID.low;
    ndir->high = NAME_$NODE_UID.high;
    ndir->low  = NAME_$NODE_UID.low;

    ACL_$EXIT_SUPER();                                      /* 0x00E74E1A */
}
