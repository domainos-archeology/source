/*
 * DISK_$AS_READ - Asynchronous single-page read for an assigned volume
 *
 * 0x00E6B860 - 0x00E6B8E8 (138 bytes).  Re-emitted from the disassembly on
 * 2026-09-08: the earlier file called argument 3 a "count pointer" and
 * tested the HIGH word of the AS_IO_SETUP status; argument 3 is the
 * caller's buffer virtual address (0x00E6B87C pushes it by value, 0x00E6B89A
 * touches its first word) and the test is `tst.w (0x2,A2)`, the low word.
 *
 * Arguments:
 *   (0x8,A6)  vol_idx_ptr  -> word volume index
 *   (0xc,A6)  daddr_ptr    -> longword disk address
 *   (0x10,A6) buffer       page-aligned virtual address, by value
 *   (0x14,A6) info         -> eight longwords: the block header DISK_IO
 *                            returns (copied out at 0x00E6B8BE-0x00E6B8CA)
 *   (0x18,A6) status       -> status_$t
 *
 * Frame: (-0x30,A6) vol_idx word, (-0x2c,A6) daddr, (-0x28,A6) wired
 * address, (-0x20,A6) the eight-longword info block.
 */

#include "disk/disk_internal.h"
#include "wp/wp.h"

/* 0x00E6B8AE: `move.w #0x2,-(SP)` - DISK_IO operation code for a read */
#define DISK_AS_OP_READ   2

void DISK_$AS_READ(uint16_t *vol_idx_ptr, uint32_t *daddr_ptr, uint32_t buffer,
                   uint32_t *info, status_$t *status)
{
    uint16_t vol_idx;           /* (-0x30,A6) */
    uint32_t daddr;             /* (-0x2c,A6) */
    uint32_t wired;             /* (-0x28,A6) */
    uint32_t local_info[8];     /* (-0x20,A6) */
    status_$t io_status;        /* D0 */
    int16_t i;

    /* 0x00E6B86A - 0x00E6B876 */
    vol_idx = *vol_idx_ptr;
    daddr = *daddr_ptr;

    /* 0x00E6B87A - 0x00E6B894: validate, wire, flush; a nonzero low status
     * word returns without unwiring */
    wired = AS_IO_SETUP(&vol_idx, buffer, status);
    if ((*status & 0xFFFF) != 0) {
        return;
    }

    /* 0x00E6B896 - 0x00E6B89A: `move.w (A1),(A1)` - touch the page */
    {
        volatile uint16_t *page = (volatile uint16_t *)ARCH_VA_TO_PTR(buffer);
        *page = *page;
    }

    /* 0x00E6B89C - 0x00E6B8BC */
    local_info[0] = 0;
    io_status = DISK_IO(DISK_AS_OP_READ, vol_idx, wired, daddr, local_info);
    *status = io_status;

    /* 0x00E6B8BE - 0x00E6B8CA: moveq #7 / dbf = eight longwords */
    for (i = 0; i < 8; i++) {
        info[i] = local_info[i];
    }

    /* 0x00E6B8CE - 0x00E6B8D6: a block header error still delivers the
     * data, so it is reported as success */
    if (io_status == status_$disk_block_header_error) {
        *status = status_$ok;
    }

    /* 0x00E6B8D8 - 0x00E6B8DC */
    WP_$UNWIRE(wired);
}
