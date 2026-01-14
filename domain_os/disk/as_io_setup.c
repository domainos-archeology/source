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

#include "disk.h"

/* Status code for buffer alignment */
#define status_$disk_buffer_not_page_aligned  0x00080013

/* Mount state offset from volume table */
#define DISK_MOUNT_STATE_OFFSET  0x90
#define DISK_MOUNT_PROC_OFFSET   0x92

/* Volume table base */
#define DISK_VOLUME_BASE  ((uint8_t *)0x00e7a1cc)

/* Page alignment mask */
#define PAGE_ALIGN_MASK  0x3ff

/* Valid volume index mask (volumes 1-10) */
#define VALID_VOL_MASK  0x7fe

/* Current process ID */
extern int16_t PROC1_$CURRENT;

/* Mount state 2 = assigned */
#define DISK_MOUNT_ASSIGNED  2

/* External functions */
extern uint32_t MST_$WIRE(uint32_t buffer, status_$t *status);
extern void CACHE_$FLUSH_VIRTUAL(void);

uint32_t AS_IO_SETUP(uint16_t *vol_idx_ptr, uint32_t buffer, status_$t *status)
{
    uint16_t vol_idx;
    int32_t offset;
    uint32_t wired_addr = 0;
    uint16_t mount_state;
    int16_t mount_proc;

    vol_idx = *vol_idx_ptr;

    /* Validate volume index (must be 1-10) */
    if ((((uint32_t)1 << (vol_idx & 0x1f)) & VALID_VOL_MASK) == 0) {
        *status = status_$invalid_volume_index;
        return wired_addr;
    }

    offset = (int16_t)(vol_idx * DISK_VOLUME_SIZE);

    /* Check buffer page alignment */
    if ((buffer & PAGE_ALIGN_MASK) != 0) {
        *status = status_$disk_buffer_not_page_aligned;
        return wired_addr;
    }

    /* Check mount state and ownership */
    mount_state = *(uint16_t *)(DISK_VOLUME_BASE + offset + DISK_MOUNT_STATE_OFFSET);
    mount_proc = *(int16_t *)(DISK_VOLUME_BASE + offset + DISK_MOUNT_PROC_OFFSET);

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
