/*
 * DISK_$DIAG_IO - Diagnostic disk I/O
 *
 * Performs diagnostic I/O operations on a physical volume.
 * Supports read and write operations with extended info blocks.
 *
 * Access control is checked based on:
 * - Volume assignment to current process
 * - Address range within volume bounds
 * - Superuser status
 * - Global diagnostic flag
 *
 * @param op_ptr       Pointer to operation (0=read, 1=write)
 * @param vol_idx_ptr  Pointer to volume index
 * @param daddr_ptr    Pointer to disk address
 * @param buffer       I/O buffer (must be page-aligned)
 * @param info         Extended info (32 bytes)
 * @param status       Output: Status code
 */

#include "acl/acl.h"
#include "cache/cache.h"
#include "disk/disk_internal.h"
#include "mmap/mmap.h"
#include "mst/mst.h"
#include "wp/wp.h"

/* Status codes */

/* Volume table base and offsets come from disk_internal.h */

/* Valid volume index mask (volumes 1-10) */
#define VALID_VOL_MASK  0x7fe

/* Page alignment mask */
#define PAGE_ALIGN_MASK  0x3ff

/* disk_$volume_t, DISK_VOL() and DISK_MOUNT_ASSIGNED come from
 * disk/disk_internal.h */

void DISK_$DIAG_IO(int16_t *op_ptr, uint16_t *vol_idx_ptr, uint32_t *daddr_ptr,
                   void *buffer, uint32_t *info, status_$t *status)
{
    int16_t op;
    uint16_t vol_idx;
    uint32_t daddr;
    disk_$volume_t *vol;
    uint16_t mount_state;
    int16_t mount_proc;
    uint32_t addr_start, addr_end;
    uint32_t wired_addr;
    uint32_t local_info[8];
    int8_t access_granted;
    int8_t direct_access;
    int16_t i;
    status_$t io_status;

    op = *op_ptr;
    vol_idx = *vol_idx_ptr;
    daddr = *daddr_ptr;

    /* Validate volume index (must be 1-10) */
    if ((((uint32_t)1 << (vol_idx & 0x1f)) & VALID_VOL_MASK) == 0) {
        *status = status_$invalid_volume_index;
        return;
    }

    vol = DISK_VOL(vol_idx);

    /* Must be a physical volume (no LV data) */
    if (vol->lv_start != 0) {
        *status = status_$operation_requires_a_physical_volume;
        return;
    }

    /* Check access permissions */
    access_granted = 0;
    direct_access = 0;

    mount_state = vol->mount_state;
    mount_proc = vol->mount_proc;

    /* Check if assigned to current process */
    if (mount_state == DISK_MOUNT_ASSIGNED && mount_proc == PROC1_$CURRENT) {
        access_granted = -1;
    }
    /* Check if read with daddr=0 */
    else if (daddr == 0 && op == 0) {
        access_granted = -1;
    }
    /* Check if address within volume bounds */
    else {
        addr_start = vol->addr_start;
        addr_end = vol->addr_end;
        if (daddr >= addr_start && daddr <= addr_end) {
            access_granted = -1;
        }
    }
    /* Check if superuser doing a read */
    if (!access_granted) {
        if (ACL_$IS_SUSER() < 0 && op == 0) {
            access_granted = -1;
        }
    }
    /* Check global diagnostic flag */
    if (!access_granted && DISK_$DIAG < 0) {
        access_granted = -1;
    }

    if (access_granted) {
        direct_access = -1;

        /* Check buffer page alignment */
        if (((uintptr_t)buffer & PAGE_ALIGN_MASK) != 0) {
            *status = status_$disk_buffer_not_page_aligned;
            return;
        }

        /* Touch buffer for read operations */
        if (op == 0) {
            *(uint16_t *)buffer &= *(uint16_t *)buffer;
        }

        /* Wire buffer for DMA */
        wired_addr = MST_$WIRE((uint32_t)(uintptr_t)buffer, status);
        CACHE_$FLUSH_VIRTUAL();
    } else {
        if (op == 0) {
            /* Allocate buffer for read without direct access */
            WP_$CALLOC(&wired_addr, status);
        } else {
            /* Write without direct access not allowed */
            *status = status_$volume_in_use;
        }
    }

    if (*status != status_$ok) {
        return;
    }

    /* Setup operation */
    int16_t io_op;
    if (op == 1) {
        io_op = 3;  /* Write with header */
        /* Copy info from caller */
        for (i = 0; i < 8; i++) {
            local_info[i] = info[i];
        }
    } else {
        io_op = 2;  /* Read */
        local_info[0] = 0;
    }

    /* Perform I/O */
    io_status = DISK_IO(io_op, vol_idx, wired_addr, daddr, local_info);
    *status = io_status;

    /* Copy info back for reads */
    if (op == 0) {
        for (i = 0; i < 8; i++) {
            info[i] = local_info[i];
        }
        /* Block header error is OK for reads */
        if (io_status == status_$disk_block_header_error) {
            *status = status_$ok;
        }
    }

    /* Cleanup */
    if (direct_access) {
        WP_$UNWIRE(wired_addr);
    } else {
        MMAP_$FREE(wired_addr);
        /* Report volume_in_use if operation succeeded without direct access */
        if (*status == status_$ok) {
            *status = status_$volume_in_use;
        }
    }
}
