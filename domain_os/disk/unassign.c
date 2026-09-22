/*
 * DISK_$UNASSIGN - Give an assigned volume back
 *
 * 0x00E6BDA8 - 0x00E6BE1A (116 bytes).  Verified against the disassembly
 * on 2026-09-19; the earlier emission was faithful.
 *
 * Arguments:
 *   (0x8,A6) vol_idx_ptr -> word (D2)
 *   (0xc,A6) status      -> status_$t (A2)
 *
 * A diskless node (NETWORK_$REALLY_DISKLESS negative, 0x00E6BDBA) reports
 * "not properly mounted" without looking at the index.  Otherwise the
 * index must be 1..10 (`btst.l` against 0x7fe) and the descriptor
 * assigned to the calling process; then DISK_$DISMOUNT is called (with a
 * discarded result slot) and the status cleared.
 */

#include "disk/disk_internal.h"
#include "network/network.h"
#include "proc1/proc1.h"

void DISK_$UNASSIGN(uint16_t *vol_idx_ptr, status_$t *status)
{
    uint16_t vol_idx = *vol_idx_ptr;    /* D2 */
    disk_$volume_t *vol;

    /* 0x00E6BDBA - 0x00E6BDC0 */
    if (NETWORK_$REALLY_DISKLESS >= 0) {
        /* 0x00E6BDC2 - 0x00E6BDD6 */
        if ((((uint32_t)VALID_VOL_MASK >> (vol_idx & 0x1f)) & 1u) == 0) {
            *status = status_$invalid_volume_index;
            return;
        }
        /* 0x00E6BDD8 - 0x00E6BDFE */
        vol = DISK_VOL(vol_idx);
        if (vol->mount_state == DISK_MOUNT_ASSIGNED &&
            (uint16_t)vol->mount_proc == PROC1_$CURRENT) {
            /* 0x00E6BE08 - 0x00E6BE10 */
            DISK_$DISMOUNT(vol_idx);
            *status = status_$ok;
            return;
        }
    }

    /* 0x00E6BE00 */
    *status = status_$volume_not_properly_mounted;
}
