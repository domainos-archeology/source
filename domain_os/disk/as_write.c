/*
 * DISK_$AS_WRITE - Asynchronous sector write
 *
 * Performs an async write operation with extended info.
 *
 * @param vol_idx_ptr  Pointer to volume index
 * @param daddr_ptr    Pointer to disk address
 * @param buffer       Buffer address (must be page-aligned)
 * @param info         Input: Extended I/O info (32 bytes)
 * @param status       Output: Status code
 */

#include "disk/disk_internal.h"
#include "wp/wp.h"

void DISK_$AS_WRITE(uint16_t *vol_idx_ptr, uint32_t *daddr_ptr, uint32_t buffer,
                    uint32_t *info, status_$t *status)
{
    uint16_t vol_idx;
    uint32_t daddr;
    uint32_t wired_addr;
    uint32_t local_info[8];
    int16_t i;
    status_$t io_status;

    vol_idx = *vol_idx_ptr;
    daddr = *daddr_ptr;

    /* Setup for async I/O - validates and wires buffer */
    wired_addr = AS_IO_SETUP(&vol_idx, buffer, status);

    if ((int16_t)((*status >> 16) & 0xFFFF) != 0) {
        return;
    }

    /* Copy extended info from caller */
    for (i = 0; i < 8; i++) {
        local_info[i] = info[i];
    }

    /* Perform the write (op=1 for write) */
    io_status = DISK_IO(1, vol_idx, (void *)(uintptr_t)wired_addr,
                        (void *)(uintptr_t)daddr, local_info);
    *status = io_status;

    /* Unwire the buffer */
    WP_$UNWIRE(wired_addr);
}
