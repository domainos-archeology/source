/*
 * PACCT_$STOP - Stop process accounting
 *
 * Disables process accounting if currently enabled. Requires locksmith
 * privileges (same check as PACCT_$START).
 *
 * Unlike PACCT_$START, this function does not return a status directly.
 * Use PACCT_$ON to verify accounting has stopped.
 *
 * Original address: 0x00E5A8C0
 * Size: 228 bytes
 */

#include "pacct_internal.h"

/*
 * Extended SID structure returned by ACL_$GET_EXSID
 */
typedef struct exsid_t {
    uid_t user_sid;     /* 0x00: User SID */
    uid_t group_sid;    /* 0x08: Group SID */
    uid_t org_sid;      /* 0x10: Org SID */
    uid_t login_sid;    /* 0x18: Login SID */
} exsid_t;

void PACCT_$STOP(void)
{
    status_$t status;
    uint8_t status_buf[8];
    exsid_t exsid;

    /* Get caller's extended SID for privilege check */
    ACL_$GET_EXSID(&exsid, &status);
    if (status != status_$ok) {
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
        /* No locksmith privilege - silently return */
        return;
    }

    /* Check if accounting is even enabled */
    if (pacct_owner.high == UID_$NIL.high &&
        pacct_owner.low == UID_$NIL.low) {
        /* Already disabled, nothing to do */
        return;
    }

    /* Unmap buffer if currently mapped */
    if (DAT_00e81804 != NULL) {
        MST_$UNMAP_PRIVI(1, &UID_$NIL, DAT_00e81804, DAT_00e81800, 0, &status);
    }

    /* Clear buffer state */
    DAT_00e81804 = NULL;    /* map_ptr = NULL */
    DAT_00e81800 = 0;       /* map_offset = 0 */
    DAT_00e817f8 = 0;       /* buf_remaining = 0 */

    /* Unlock the accounting file */
    FILE_$PRIV_UNLOCK(&pacct_owner, (int16_t)DAT_00e817f4, 0x40000,
                      0, 0, 0, status_buf, &status);

    /* Disable accounting by setting owner to nil */
    pacct_owner.high = UID_$NIL.high;
    pacct_owner.low = UID_$NIL.low;
}
