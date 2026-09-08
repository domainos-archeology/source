/*
 * PACCT_$START - Start process accounting
 *
 * Enables process accounting to the specified file. Requires locksmith
 * privileges (caller must have user, group, or org SID matching
 * RGYC_$G_LOCKSMITH_UID).
 *
 * If accounting is already enabled, shuts down existing accounting first.
 *
 * Original address: 0x00E5A746
 * Size: 370 bytes
 */

#include "pacct/pacct_internal.h"

/*
 * 0x00E5A8BC: longword 0.  Handed to FILE_$PRIV_LOCK by reference as its
 * ACL-context argument (`pea (0x84,PC)` at 0x00E5A836).
 */
static void *pacct_$start_nil_acl_ctx = NULL;

/*
 * Constant cells in the code region, passed by reference to
 * FILE_$GET_ATTR_INFO at 0x00E5A86A (`pea (0x4c,PC)` -> 0xE5A8B8) and
 * 0x00E5A86E (`pea (0x4a,PC)` -> 0xE5A8BA).  The image holds 0x007A - the
 * compact record's size - and 0x0401, whose second byte sets bit 0, i.e.
 * "the caller already holds the lock" (this routine has just taken it).
 */
static int16_t  pacct_$start_attr_info_size = FILE_ATTR_INFO_SIZE;  /* 0xE5A8B8 */
static uint16_t pacct_$start_attr_info_req  = 0x0401;               /* 0xE5A8BA */

/*
 * Extended SID structure returned by ACL_$GET_EXSID
 * Contains user, group, and org SIDs for privilege checking
 */
typedef struct exsid_t {
    uid_t user_sid;     /* 0x00: User SID (at offset -0x68 from stack) */
    uid_t group_sid;    /* 0x08: Group SID (at offset -0x60) */
    uid_t org_sid;      /* 0x10: Org SID (at offset -0x58) */
    uid_t login_sid;    /* 0x18: Login SID (at offset -0x50) */
} exsid_t;

void PACCT_$START(uid_t *file_uid, uint32_t unused, status_$t *status_ret)
{
    exsid_t exsid;
    /* (-0x112,A6): FILE_$PRIV_LOCK's rights word out. */
    uint16_t rights_out;
    /* (-0x108,A6): the 0x20-byte object-location record FILE_$GET_ATTR_INFO
     * rewrites; nothing here reads it back. */
    file_$obj_loc_t loc_rec;
    /* (-0xE8,A6): the 0x7A-byte compact attribute record. */
    uint8_t file_info[FILE_ATTR_INFO_SIZE];
    uint8_t file_type;      /* At offset -0xe7 in original */
    uint32_t file_len;      /* At offset -0xd4 in original */

    (void)unused;

    /* Get caller's extended SID for privilege check */
    ACL_$GET_EXSID(&exsid, status_ret);
    if (*status_ret != status_$ok) {
        return;
    }

    /*
     * Check for locksmith privilege
     * Any of user, group, or org SID matching locksmith grants access
     */
    if ((exsid.login_sid.high != RGYC_$G_LOCKSMITH_UID.high ||
         exsid.login_sid.low != RGYC_$G_LOCKSMITH_UID.low) &&
        /* 0x00E5A784 compares (-0x60,A6) = exsid + 8 = group_sid. */
        (exsid.group_sid.high != RGYC_$G_LOCKSMITH_UID.high ||
         exsid.group_sid.low != RGYC_$G_LOCKSMITH_UID.low) &&
        (exsid.user_sid.high != RGYC_$G_LOCKSMITH_UID.high ||
         exsid.user_sid.low != RGYC_$G_LOCKSMITH_UID.low)) {
        *status_ret = status_$insufficient_rights_to_perform_operation;
        return;
    }

    /* Shutdown any existing accounting */
    if (pacct_owner.high != UID_$NIL.high ||
        pacct_owner.low != UID_$NIL.low) {
        /* (-0x110,A6): FILE_$PRIV_UNLOCK's data-time-valid longword out. */
        uint32_t dtv_out;

        /* Unmap buffer if mapped */
        if (pacct_map_ptr != NULL) {
            MST_$UNMAP_PRIVI(1, &UID_$NIL, ARCH_PTR_TO_VA(pacct_map_ptr), pacct_map_offset, 0, status_ret);
        }

        /* Clear buffer state */
        pacct_map_ptr = NULL;
        pacct_map_offset = 0;
        pacct_buf_remaining = 0;

        /* Unlock the old file */
        /* 0x00E5A7FA-0x00E5A812: `move.l (0x8,A5)` slot, `move.l #0x40000`
         * = mode word 4 + asid word 0, then three `clr.l`. */
        /* 0x00E5A7FA `pea (A3)`: the status goes to the caller's status_ret,
         * not to a local. */
        (void)FILE_$PRIV_UNLOCK(&pacct_owner, (int32_t)pacct_lock_handle, 4, 0,
                                0, 0, 0, 0, &dtv_out, status_ret);
    }

    /* Reset owner to nil */
    pacct_owner.high = UID_$NIL.high;
    pacct_owner.low = UID_$NIL.low;

    /* Lock the new accounting file exclusively with write access */
    /* 0x00E5A836 `pea (0x84,PC)` = the NIL longword at 0x00E5A8BC; the
     * compiler passes its address, not a null pointer. */
    FILE_$PRIV_LOCK(file_uid, 0, 1, 4, 0, 0x0008, 0x0000,
                    0, 0, 0, &pacct_$start_nil_acl_ctx,
                    0, &pacct_lock_handle, &rights_out, status_ret);

    if (*status_ret != status_$ok) {
        return;
    }

    /* Get file attributes to verify it's a regular file (0x00E5A860) */
    FILE_$GET_ATTR_INFO(file_uid, &pacct_$start_attr_info_req,
                        &pacct_$start_attr_info_size,
                        &loc_rec, file_info, status_ret);

    if (*status_ret != status_$ok) {
        return;
    }

    /*
     * Check file type - byte at offset 0x01 in file_info
     * (this corresponds to -0xe7 from frame pointer)
     * Type 0 = regular file
     */
    file_type = file_info[1];
    if (file_type != 0) {
        *status_ret = status_$no_rights;
        return;
    }

    /* Get file length from attributes (at offset 0x14 from file_info start) */
    file_len = *(uint32_t *)(file_info + 0x14);

    /* Initialize accounting state */
    pacct_file_pos = file_len;    /* Current file position = file length */
    pacct_map_ptr = NULL;        /* No mapping yet */
    pacct_write_ptr = NULL;        /* No write pointer yet */
    pacct_buf_remaining = 0;           /* No buffer space yet */

    /* Set accounting file owner */
    pacct_owner.high = file_uid->high;
    pacct_owner.low = file_uid->low;
}
