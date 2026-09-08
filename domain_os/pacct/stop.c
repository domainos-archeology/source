/*
 * PACCT_$STOP - Stop process accounting
 *
 * Disables process accounting if currently enabled. Requires locksmith
 * privileges (same check as PACCT_$START).
 *
 * Unlike PACCT_$START, this function does not return a status directly: the
 * only status cell it has is the frame local at A6-0x74, which nothing reads
 * back.  Use PACCT_$ON to verify accounting has stopped.
 *
 * The routine has no return value either - the epilogue at 0x00E5A99C is
 * `movea.l (-0x78,A6),A5` / `unlk A6` / `rts` with no store to D0.
 *
 * Original address: 0x00E5A8C0
 * Size: 228 bytes
 */

#include "pacct/pacct_internal.h"

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
    status_$t status;       /* A6-0x74 */
    /* (-0x70,A6): FILE_$PRIV_UNLOCK's data-time-valid longword out. */
    uint32_t dtv_out;
    exsid_t exsid;          /* A6-0x68 */

    /* 0x00E5A8CC-0x00E5A8DA: two var parameters, the record then the status. */
    ACL_$GET_EXSID(&exsid, &status);
    if (status != status_$ok) {         /* 0x00E5A8DC-0x00E5A8E0 */
        return;
    }

    /*
     * 0x00E5A8E4-0x00E5A928: three `cmpm.l` pairs against
     * RGYC_$G_LOCKSMITH_UID, tried in the order login, group, user.  Any
     * match grants access; otherwise the routine writes a status into its
     * OWN frame cell and returns.
     */
    if ((exsid.login_sid.high != RGYC_$G_LOCKSMITH_UID.high ||
         exsid.login_sid.low != RGYC_$G_LOCKSMITH_UID.low) &&
        /* 0x00E5A8F8 compares (-0x60,A6) = exsid + 8 = group_sid. */
        (exsid.group_sid.high != RGYC_$G_LOCKSMITH_UID.high ||
         exsid.group_sid.low != RGYC_$G_LOCKSMITH_UID.low) &&
        (exsid.user_sid.high != RGYC_$G_LOCKSMITH_UID.high ||
         exsid.user_sid.low != RGYC_$G_LOCKSMITH_UID.low)) {
        /*
         * 0x00E5A920 `move.l #0x230002,(-0x74,A6)`.  PACCT_$STOP has no
         * status parameter, so this only ever reaches the frame cell that
         * the epilogue discards - but the image writes it, so we write it.
         * (source-0tzz)
         */
        status = status_$insufficient_rights_to_perform_operation;
        (void)status;
        return;
    }

    /*
     * 0x00E5A92A-0x00E5A93A: `cmpm.l` pair of pacct_owner against UID_$NIL.
     * When accounting is already off the image branches STRAIGHT to
     * 0x00E5A990 - it skips the unmap and the unlock, but it still falls
     * through the "pacct_owner = UID_$NIL" store below.  There is no early
     * return.  (source-0tzz)
     */
    if (pacct_state.owner.high != UID_$NIL.high ||
        pacct_state.owner.low != UID_$NIL.low) {

        /* 0x00E5A93C-0x00E5A962: unmap the accounting buffer if one is
         * mapped.  The start VA goes BY VALUE (`move.l (0x18,A5),-(SP)`). */
        if (pacct_state.map_ptr != NULL) {
            MST_$UNMAP_PRIVI(1, &UID_$NIL, ARCH_PTR_TO_VA(pacct_state.map_ptr),
                             pacct_state.map_offset, 0, &status);
        }

        /* 0x00E5A964-0x00E5A96E */
        pacct_state.map_ptr       = NULL;   /* (0x18,A5) */
        pacct_state.map_offset    = 0;      /* (0x14,A5) */
        pacct_state.buf_remaining = 0;      /* (0x0C,A5) */

        /*
         * 0x00E5A970-0x00E5A98E: unlock the accounting file.  The image never
         * cleans the 32 bytes it pushed - the `unlk A6` at 0x00E5A9A0 does
         * that - and it discards the returned boolean.
         *
         * `move.l #0x40000` is the lock_mode word 4 followed by the asid word
         * 0; the three `clr.l` cover by_key/key, rem_key and rem_node.
         */
        (void)FILE_$PRIV_UNLOCK(&pacct_state.owner,
                                (int32_t)pacct_state.lock_handle, 4, 0,
                                0, 0, 0, 0, &dtv_out, &status);
    }

    /*
     * 0x00E5A990-0x00E5A99A: `movea.l #0xe1737c,A0` then two `move.l (A0)+`.
     * Reached on BOTH arms - the store is redundant when the owner was
     * already nil, and the image performs it anyway.  (source-0tzz)
     */
    pacct_state.owner.high = UID_$NIL.high;
    pacct_state.owner.low  = UID_$NIL.low;
}
