/*
 * BAT_$N_FREE - Get free block count for a volume
 *
 * Returns the number of free and total blocks on a volume.
 *
 * Original address: 0x00E3B9E4
 * Original size: 144 bytes
 */

#include "bat/bat_internal.h"
#include "network/network.h"

/*
 * BAT_$N_FREE
 *
 * Parameters:
 *   vol_idx_ptr - Pointer to volume index
 *   free_out    - Output receiving free block count
 *   total_out   - Output receiving total block count
 *   status      - Output status code
 *
 * Assembly analysis:
 *   - 0x00E3B9FC: checks NETWORK_$REALLY_DISKLESS first; that is the ONLY
 *     path that leaves free_out and total_out alone
 *   - 0x00E3BA0E: takes ML_LOCK_BAT (0x11)
 *   - 0x00E3BA1A-0x00E3BA36: validates volume index (1..6) and mount status
 *   - 0x00E3BA42-0x00E3BA46: on success loads free_blocks into D3 and
 *     total_blocks into D2
 *   - 0x00E3BA5A-0x00E3BA66: stores D3, D2 and the status UNCONDITIONALLY,
 *     that is, on the not-mounted path too
 */
void BAT_$N_FREE(uint16_t *vol_idx_ptr, uint32_t *free_out, uint32_t *total_out,
                 status_$t *status)
{
    uint16_t vol_idx;
    status_$t local_status;

    /*
     * D3 and D2, the two registers the epilogue stores into the caller's
     * cells at 0x00E3BA5A-0x00E3BA64.
     *
     * On the not-mounted path (0x00E3BA38) neither is assigned, so the image
     * hands the caller register residue: D3 is whatever the caller had in it
     * (BAT_$N_FREE saves and restores it at 0x00E3B9E8 / 0x00E3BA6A but never
     * writes it before the store), and D2 is the caller's D2 with its LOW
     * word replaced by the volume index - `move.w (A0),D2w` at 0x00E3B9FA
     * writes only the low half.
     *
     * C cannot spell "indeterminate", and giving these an initialiser would
     * be an invention, so they are left unassigned and the store is compiled
     * with the uninitialised-use diagnostic suppressed.  The values are
     * meaningless on that path in the image as well; callers must look at
     * *status, exactly as they must there.
     */
    uint32_t free_blocks;       /* D3 */
    uint32_t total_blocks;      /* D2, low word = vol_idx */

    /* 0x00E3B9F6-0x00E3B9FA */
    vol_idx = *vol_idx_ptr;

    /* 0x00E3B9FC tst.b / bpl - only the diskless arm skips the stores */
    if (NETWORK_$REALLY_DISKLESS < 0) {
        *status = bat_$not_mounted;
        return;
    }

    /* 0x00E3BA0C-0x00E3BA18 */
    ML_$LOCK(ML_LOCK_BAT);

    /*
     * 0x00E3BA1A-0x00E3BA36: 0 < vol_idx <= 6 and bat_$mounted[vol_idx] < 0.
     *
     * The suppression covers the test as well as the store, because that is
     * where a compiler reports the conditionally-uninitialised use.
     */
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wuninitialized"
#endif
    if (vol_idx == 0 || vol_idx > BAT_MAX_VOL_INDEX || bat_$mounted[vol_idx] >= 0) {
        /* 0x00E3BA38 */
        local_status = bat_$not_mounted;
    } else {
        /* 0x00E3BA42-0x00E3BA4A: volume is mounted, return statistics */
        free_blocks = bat_$volumes[vol_idx].free_blocks;    /* (-0x230,A0) */
        total_blocks = bat_$volumes[vol_idx].total_blocks;  /* (-0x234,A0) */
        local_status = status_$ok;
    }

    /* 0x00E3BA4E-0x00E3BA58 */
    ML_$UNLOCK(ML_LOCK_BAT);

    /* 0x00E3BA5A-0x00E3BA66: all three stores happen on both arms */
    *free_out = free_blocks;
    *total_out = total_blocks;
    *status = local_status;
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
}
