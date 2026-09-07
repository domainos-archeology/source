/*
 * AS_IO_SETUP - Setup for async I/O operations
 *
 * Internal helper function that validates volume index and mount state,
 * then wires the buffer pages for DMA access.
 *
 * @param vol_idx_ptr  Pointer to volume index
 * @param buffer       Buffer address (must be page-aligned)
 * @param status       Output: Status code
 * @return             Wired address for I/O, or undefined on error
 */

#include "cache/cache.h"
#include "disk/disk_internal.h"
#include "mst/mst.h"

/* Status code for buffer alignment */

/* disk_$volume_t and DISK_VOL() come from disk_internal.h */

/* Page alignment mask */
#define PAGE_ALIGN_MASK  0x3ff

/* Valid volume index mask (volumes 1-10) */
#define VALID_VOL_MASK  0x7fe

/* Mount state 2 = assigned */
#define DISK_MOUNT_ASSIGNED  2

uint32_t AS_IO_SETUP(uint16_t *vol_idx_ptr, uint32_t buffer, status_$t *status)
{
    uint16_t vol_idx;
    disk_$volume_t *vol;
    uint32_t wired_addr = 0;
    uint16_t mount_state;
    int16_t mount_proc;

    vol_idx = *vol_idx_ptr;

    /* Validate volume index (must be 1-10) */
    if ((((uint32_t)1 << (vol_idx & 0x1f)) & VALID_VOL_MASK) == 0) {
        *status = status_$invalid_volume_index;
        return wired_addr;
    }

    vol = DISK_VOL(vol_idx);

    /* Check buffer page alignment */
    if ((buffer & PAGE_ALIGN_MASK) != 0) {
        *status = status_$disk_buffer_not_page_aligned;
        return wired_addr;
    }

    /* Check mount state and ownership */
    mount_state = vol->mount_state;
    mount_proc = vol->mount_proc;

    if (mount_state != DISK_MOUNT_ASSIGNED || mount_proc != PROC1_$CURRENT) {
        *status = status_$volume_not_properly_mounted;
        return wired_addr;
    }

    /* Wire the buffer for DMA access */
    wired_addr = MST_$WIRE(buffer, status);

    /* Set high bit of status if error */
    if ((int16_t)((*status >> 16) & 0xFFFF) != 0) {
        *(uint8_t *)status |= 0x80;
    }

    /* Flush cache for DMA coherency */
    CACHE_$FLUSH_VIRTUAL();

    return wired_addr;
}
