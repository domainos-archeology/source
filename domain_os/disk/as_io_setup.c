/*
 * AS_IO_SETUP - Validate and wire the buffer for an async disk transfer
 *
 * 0x00E6B74C - 0x00E6B7E2 (152 bytes, first routine of the second `DISK_`
 * code segment at 0xE6B6DC).  Module-local (no `$` in the map name); the
 * callers are DISK_$AS_READ (0x00E6B860), DISK_$AS_WRITE (0x00E6B8EA) and
 * DISK_$AS_XFER_MULTI (0x00E6B962).  Re-emitted from the disassembly on
 * 2026-09-08: the earlier file tested the HIGH word of the MST_$WIRE status
 * before setting bit 31; the image tests the low word (`tst.w (0x2,A2)`).
 *
 * Arguments:
 *   (0x8,A6)  vol_idx_ptr  -> word volume index (Pascal var parameter)
 *   (0xc,A6)  buffer       virtual address of the caller's page, by value
 *   (0x10,A6) status       -> status_$t
 *
 * Result: D2, which is only assigned on the success path (the wired
 * address MST_$WIRE returned).  On the three error paths the original
 * returns whatever the caller left in D2; this emission returns 0 there.
 */

#include "cache/cache.h"
#include "disk/disk_internal.h"
#include "mst/mst.h"
#include "proc1/proc1.h"

/* 0x00E6B75E: `move.l #0x7fe,D1` / `btst.l D0,D1` - volumes 1..10 */
#define DISK_AS_VALID_VOL_MASK   0x7feu
/* 0x00E6B78C: `andi.l #0x3ff,D0` - the buffer must start a 1K page */
#define DISK_AS_PAGE_ALIGN_MASK  0x3ffu
/* 0x00E6B79C: `cmpi.w #0x2,(-0x34,A0)` - mount_state "assigned" */
#define DISK_MOUNT_STATE_ASSIGNED 2

uint32_t AS_IO_SETUP(uint16_t *vol_idx_ptr, uint32_t buffer, status_$t *status)
{
    uint16_t vol_idx;
    disk_$volume_t *vol;
    uint32_t wired = 0;

    /* 0x00E6B754 - 0x00E6B770: btst.l D0,D1 numbers the bit modulo 32 */
    vol_idx = *vol_idx_ptr;
    if (((DISK_AS_VALID_VOL_MASK >> (vol_idx & 0x1f)) & 1u) == 0) {
        *status = status_$invalid_volume_index;
        return wired;
    }

    /* 0x00E6B772 - 0x00E6B786: 0xE7A290 + vol_idx * 0x48, word arithmetic */
    vol = DISK_VOL(vol_idx);

    /* 0x00E6B788 - 0x00E6B79A */
    if ((buffer & DISK_AS_PAGE_ALIGN_MASK) != 0) {
        *status = status_$disk_buffer_not_page_aligned;
        return wired;
    }

    /* 0x00E6B79C - 0x00E6B7B6: assigned to the calling process? */
    if (vol->mount_state != DISK_MOUNT_STATE_ASSIGNED ||
        (uint16_t)vol->mount_proc != PROC1_$CURRENT) {
        *status = status_$volume_not_properly_mounted;
        return wired;
    }

    /* 0x00E6B7B8 - 0x00E6B7C6 */
    wired = MST_$WIRE(buffer, status);

    /* 0x00E6B7C8 - 0x00E6B7CE: `tst.w (0x2,A2)` is the LOW word of the
     * status; `bset.b #7,(A2)` is bit 31 */
    if ((*status & 0xFFFF) != 0) {
        *status |= (status_$t)0x80000000;
    }

    /* 0x00E6B7D2 */
    CACHE_$FLUSH_VIRTUAL();

    return wired;
}
