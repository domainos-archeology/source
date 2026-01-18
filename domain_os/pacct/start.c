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

#include "pacct_internal.h"

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
    uint8_t status_buf[4];
    uint8_t attr_buf[32];
    uint8_t file_info[64];
    uint8_t file_type;      /* At offset -0xe7 in original */
    uint32_t file_len;      /* At offset -0xd4 in original */
    int16_t attr_size;
    int16_t attr_flags;

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
        (exsid.org_sid.high != RGYC_$G_LOCKSMITH_UID.high ||
         exsid.org_sid.low != RGYC_$G_LOCKSMITH_UID.low) &&
        (exsid.user_sid.high != RGYC_$G_LOCKSMITH_UID.high ||
         exsid.user_sid.low != RGYC_$G_LOCKSMITH_UID.low)) {
        *status_ret = status_$insufficient_rights_to_perform_operation;
        return;
    }

    /* Shutdown any existing accounting */
    if (pacct_owner.high != UID_$NIL.high ||
        pacct_owner.low != UID_$NIL.low) {
        status_$t local_status;
        uint8_t unlock_buf[8];

        /* Unmap buffer if mapped */
        if (DAT_00e81804 != NULL) {
            MST_$UNMAP_PRIVI(1, &UID_$NIL, DAT_00e81804, DAT_00e81800, 0, status_ret);
        }

        /* Clear buffer state */
        DAT_00e81804 = NULL;
        DAT_00e81800 = 0;
        DAT_00e817f8 = 0;

        /* Unlock the old file */
        FILE_$PRIV_UNLOCK(&pacct_owner, (int16_t)DAT_00e817f4, 0x40000,
                          0, 0, 0, unlock_buf, &local_status);
    }

    /* Reset owner to nil */
    pacct_owner.high = UID_$NIL.high;
    pacct_owner.low = UID_$NIL.low;

    /* Lock the new accounting file exclusively with write access */
    attr_size = 0;  /* Size placeholder for callback pointer location */
    FILE_$PRIV_LOCK(file_uid, 0, 1, 4, 0, 0x80000,
                    0, 0, 0, NULL /* callback at 0xe5a8bc */,
                    0, &DAT_00e817f4, status_buf, status_ret);

    if (*status_ret != status_$ok) {
        return;
    }

    /* Get file attributes to verify it's a regular file */
    attr_size = 0x7A;   /* Size constant at 0xe5a8b8 */
    attr_flags = 0;     /* Flags at 0xe5a8ba */
    FILE_$GET_ATTR_INFO(file_uid, &attr_flags, &attr_size,
                        (uint32_t *)attr_buf, file_info, status_ret);

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
    DAT_00e81808 = file_len;    /* Current file position = file length */
    DAT_00e81804 = NULL;        /* No mapping yet */
    DAT_00e817fc = NULL;        /* No write pointer yet */
    DAT_00e817f8 = 0;           /* No buffer space yet */

    /* Set accounting file owner */
    pacct_owner.high = file_uid->high;
    pacct_owner.low = file_uid->low;
}
