/*
 * DISK_$DIAG_IO - Diagnostic single-page read or write on a physical volume
 *
 * 0x00E6BC18 - 0x00E6BDA6 (400 bytes).  Re-emitted from the disassembly on
 * 2026-09-08: the control flow of the earlier file was right; the buffer
 * argument is now the 32-bit VA the image passes by value (0x00E6BCBC
 * `move.l (0x14,A6),D2`) rather than a host pointer.
 *
 * Arguments:
 *   (0x8,A6)  op_ptr       -> word: 0 = read, 1 = write (D4)
 *   (0xc,A6)  vol_idx_ptr  -> word volume index (D5)
 *   (0x10,A6) daddr_ptr    -> longword disk address (D6)
 *   (0x14,A6) buffer       page-aligned VA, by value; only used when the
 *                          caller is allowed to use its own page
 *   (0x18,A6) info         -> eight-longword block header (in for a write,
 *                          out for a read)
 *   (0x1c,A6) status       -> status_$t
 *
 * Whether the caller's page is used is the boolean D3 (`st D3b` at
 * 0x00E6BCB6), set by any one of:
 *   - the volume is assigned to the calling process (0x00E6BC78)
 *   - a read of disk address 0 (0x00E6BC8C)
 *   - the address lies inside [addr_start, addr_end] (0x00E6BC94)
 *   - a read by the super-user (0x00E6BCA0)
 *   - DISK_$DIAG is set (0x00E6BCAE)
 * Otherwise a read goes through a kernel page from WP_$CALLOC and is
 * reported as "volume in use" even when it worked (0x00E6BD98); a write
 * is refused outright (0x00E6BD0A).
 */

#include "acl/acl.h"
#include "cache/cache.h"
#include "disk/disk_internal.h"
#include "mmap/mmap.h"
#include "mst/mst.h"
#include "proc1/proc1.h"
#include "wp/wp.h"

/* 0x00E6BCC2: `andi.l #0x3ff,D0` */
#define DISK_DIAG_PAGE_ALIGN_MASK  0x3ffu
/* 0x00E6BD1C / 0x00E6BD34: the DISK_IO operation codes used here */
#define DISK_DIAG_OP_WRITE_HDR     3
#define DISK_DIAG_OP_READ          2

void DISK_$DIAG_IO(int16_t *op_ptr, uint16_t *vol_idx_ptr, uint32_t *daddr_ptr,
                   uint32_t buffer, uint32_t *info, status_$t *status)
{
    int16_t op;                 /* D4 */
    uint16_t vol_idx;           /* D5 */
    uint32_t daddr;             /* D6 */
    disk_$volume_t *vol;        /* A0 */
    int8_t own_page;            /* D3 */
    uint32_t wired;             /* (-0x2c,A6) */
    uint16_t io_op;             /* (-0x34,A6) */
    uint32_t local_info[8];     /* (-0x20,A6) */
    status_$t io_status;        /* D0 */
    int16_t i;

    /* 0x00E6BC20 - 0x00E6BC3A */
    op = *op_ptr;
    vol_idx = *vol_idx_ptr;
    daddr = *daddr_ptr;

    /* 0x00E6BC3C - 0x00E6BC4E: `btst.l D0,D1` against 0x7fe, modulo 32 */
    if ((((uint32_t)VALID_VOL_MASK >> (vol_idx & 0x1f)) & 1u) == 0) {
        *status = status_$invalid_volume_index;
        return;
    }

    /* 0x00E6BC52 - 0x00E6BC62: 0xE7A290 + vol_idx * 0x48, word arithmetic */
    vol = DISK_VOL(vol_idx);

    /* 0x00E6BC66 - 0x00E6BC72: lv_start (-0x40) must be zero */
    if (vol->lv_start != 0) {
        *status = status_$operation_requires_a_physical_volume;
        return;
    }

    /* 0x00E6BC76 - 0x00E6BCB6 */
    own_page = 0;
    if (vol->mount_state == DISK_MOUNT_ASSIGNED &&
        (uint16_t)vol->mount_proc == PROC1_$CURRENT) {
        own_page = -1;
    } else if (daddr == 0 && op == 0) {
        own_page = -1;
    } else if (daddr >= vol->addr_start && daddr <= vol->addr_end) {
        own_page = -1;
    } else if (ACL_$IS_SUSER() < 0 && op == 0) {
        own_page = -1;
    } else if (DISK_$DIAG < 0) {
        own_page = -1;
    }

    /* 0x00E6BCB8 */
    if (own_page < 0) {
        /* 0x00E6BCBC - 0x00E6BCD0 */
        if ((buffer & DISK_DIAG_PAGE_ALIGN_MASK) != 0) {
            *status = status_$disk_buffer_not_page_aligned;
            return;
        }
        /* 0x00E6BCD4 - 0x00E6BCDC: a read touches the page first
         * (`move.w (A0),D0w` / `and.w D0w,(A0)`) */
        if (op == 0) {
            volatile uint16_t *page = (volatile uint16_t *)ARCH_VA_TO_PTR(buffer);
            uint16_t w = *page;
            *page &= w;
        }
        /* 0x00E6BCDE - 0x00E6BCEE */
        wired = MST_$WIRE(buffer, status);
        CACHE_$FLUSH_VIRTUAL();
    } else if (op == 0) {
        /* 0x00E6BCFA - 0x00E6BD06 */
        WP_$CALLOC(&wired, status);
    } else {
        /* 0x00E6BD0A */
        *status = status_$volume_in_use;
    }

    /* 0x00E6BD10 - 0x00E6BD12 */
    if (*status != status_$ok) {
        return;
    }

    /* 0x00E6BD16 - 0x00E6BD3A */
    if (op == 1) {
        io_op = DISK_DIAG_OP_WRITE_HDR;
        for (i = 0; i < 8; i++) {                       /* moveq #7 / dbf */
            local_info[i] = info[i];
        }
    } else {
        io_op = DISK_DIAG_OP_READ;
        local_info[0] = 0;
    }

    /* 0x00E6BD3E - 0x00E6BD58 */
    io_status = DISK_IO(io_op, vol_idx, wired, daddr, local_info);
    *status = io_status;

    /* 0x00E6BD5A - 0x00E6BD76: only op == 0 copies the header back and
     * forgives a block header error */
    if (op == 0) {
        for (i = 0; i < 8; i++) {
            info[i] = local_info[i];
        }
        if (io_status == status_$disk_block_header_error) {
            *status = status_$ok;
        }
    }

    /* 0x00E6BD78 - 0x00E6BD98 */
    if (own_page < 0) {
        WP_$UNWIRE(wired);
    } else {
        MMAP_$FREE(wired);
        if (*status == status_$ok) {
            *status = status_$volume_in_use;
        }
    }
}
