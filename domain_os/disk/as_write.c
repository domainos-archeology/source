/*
 * DISK_$AS_WRITE - Asynchronous single-page write for an assigned volume
 *
 * 0x00E6B8EA - 0x00E6B960 (120 bytes).  Re-emitted from the disassembly on
 * 2026-09-08.  Unlike DISK_$AS_READ this copies the caller's eight
 * header longwords IN before the transfer (0x00E6B920-0x00E6B92E), does
 * not touch the page, and does not remap the block-header-error status.
 *
 * Arguments:
 *   (0x8,A6)  vol_idx_ptr  -> word volume index
 *   (0xc,A6)  daddr_ptr    -> longword disk address
 *   (0x10,A6) buffer       page-aligned virtual address, by value
 *   (0x14,A6) info         -> eight longwords: the block header to write
 *   (0x18,A6) status       -> status_$t
 *
 * Frame: (-0x30,A6) vol_idx word, (-0x2c,A6) daddr, (-0x28,A6) wired
 * address, (-0x20,A6) the eight-longword info block.
 */

#include "disk/disk_internal.h"
#include "wp/wp.h"

/* 0x00E6B940: `move.w #0x1,-(SP)` - DISK_IO operation code for a write */
#define DISK_AS_OP_WRITE  1

void DISK_$AS_WRITE(uint16_t *vol_idx_ptr, uint32_t *daddr_ptr, uint32_t buffer,
                    uint32_t *info, status_$t *status)
{
    uint16_t vol_idx;           /* (-0x30,A6) */
    uint32_t daddr;             /* (-0x2c,A6) */
    uint32_t wired;             /* (-0x28,A6) */
    uint32_t local_info[8];     /* (-0x20,A6) */
    int16_t i;

    /* 0x00E6B8F4 - 0x00E6B900 */
    vol_idx = *vol_idx_ptr;
    daddr = *daddr_ptr;

    /* 0x00E6B904 - 0x00E6B91E: validate, wire, flush; a nonzero low status
     * word returns without unwiring */
    wired = AS_IO_SETUP(&vol_idx, buffer, status);
    if ((*status & 0xFFFF) != 0) {
        return;
    }

    /* 0x00E6B920 - 0x00E6B92E: moveq #7 / dbf = eight longwords in */
    for (i = 0; i < 8; i++) {
        local_info[i] = info[i];
    }

    /* 0x00E6B932 - 0x00E6B94E */
    *status = DISK_IO(DISK_AS_OP_WRITE, vol_idx, wired, daddr, local_info);

    /* 0x00E6B950 - 0x00E6B954 */
    WP_$UNWIRE(wired);
}
