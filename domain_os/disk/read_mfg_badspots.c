/*
 * DISK_$READ_MFG_BADSPOTS - Read the manufacturer's bad-spot page
 *
 * 0x00E6B7E4 - 0x00E6B85E (124 bytes).  Re-emitted from the disassembly on
 * 2026-09-19: the earlier file tested the HIGH word of the AS_IO_SETUP
 * status (`tst.w (0x2,A2)` at 0x00E6B816 is the low word) and named the
 * arguments as if the third were a count.
 *
 * Arguments:
 *   (0x8,A6)  vol_idx_ptr  -> word, copied to (-0x34,A6)
 *   (0xc,A6)  daddr_ptr    -> longword (D2)
 *   (0x10,A6) buffer       page-aligned VA by value (A3), given to
 *                          AS_IO_SETUP
 *   (0x14,A6) status       -> status_$t (A2)
 *
 * DISK_IO op 4 (`move.w #0x4`, 0x00E6B832) with an info block whose
 * first longword is cleared; a block header error is reported as success
 * (0x00E6B842 - 0x00E6B84A); the page is unwired afterwards.
 */

#include "disk/disk_internal.h"
#include "wp/wp.h"

/* 0x00E6B832: DISK_IO's operation code for the bad-spot read */
#define DISK_MFG_BADSPOTS_OP   4

void DISK_$READ_MFG_BADSPOTS(uint16_t *vol_idx_ptr, uint32_t *daddr_ptr,
                             uint32_t buffer, status_$t *status)
{
    uint16_t vol_idx;           /* (-0x34,A6) */
    uint32_t daddr;             /* D2 / (-0x24,A6) */
    uint32_t wired;             /* (-0x2c,A6) */
    uint32_t local_info[8];     /* (-0x20,A6) */
    status_$t io_status;        /* D0 */

    /* 0x00E6B7F4 - 0x00E6B800 */
    vol_idx = *vol_idx_ptr;
    daddr = *daddr_ptr;

    /* 0x00E6B802 - 0x00E6B81A */
    wired = AS_IO_SETUP(&vol_idx, buffer, status);
    if ((*status & 0xFFFF) != 0) {
        return;
    }

    /* 0x00E6B81C - 0x00E6B840 */
    local_info[0] = 0;
    io_status = DISK_IO(DISK_MFG_BADSPOTS_OP, vol_idx, wired, daddr, local_info);
    *status = io_status;

    /* 0x00E6B842 - 0x00E6B84A */
    if (io_status == status_$disk_block_header_error) {
        *status = status_$ok;
    }

    /* 0x00E6B84C - 0x00E6B850 */
    WP_$UNWIRE(wired);
}
