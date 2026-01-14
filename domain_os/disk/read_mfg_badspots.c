/*
 * DISK_$READ_MFG_BADSPOTS - Read manufacturer bad spot information
 *
 * Reads the manufacturing bad spot data from a disk volume.
 * Block header errors are treated as OK for this operation.
 *
 * @param vol_idx_ptr  Pointer to volume index
 * @param buffer_ptr   Pointer to destination buffer address
 * @param count        Transfer count/buffer parameter
 * @param status       Output: Status code
 */

#include "disk/disk_internal.h"
#include "wp/wp.h"

/* Block header error status */
#define status_$disk_block_header_error  0x00080011

void DISK_$READ_MFG_BADSPOTS(uint16_t *vol_idx_ptr, uint32_t *buffer_ptr,
                              uint32_t count, status_$t *status)
{
    uint16_t vol_idx;
    uint32_t buffer;
    uint32_t wired_addr;
    uint32_t local_info[8];
    status_$t io_status;

    vol_idx = *vol_idx_ptr;
    buffer = *buffer_ptr;

    /* Setup for async I/O - validates and wires buffer */
    wired_addr = AS_IO_SETUP(&vol_idx, count, status);

    if ((int16_t)((*status >> 16) & 0xFFFF) != 0) {
        return;
    }

    /* Initialize info */
    local_info[0] = 0;

    /* Perform the read (op=4 for manufacturing bad spots) */
    io_status = DISK_IO(4, vol_idx, (void *)(uintptr_t)wired_addr,
                        (void *)(uintptr_t)buffer, local_info);
    *status = io_status;

    /* Block header error is OK for this operation */
    if (io_status == status_$disk_block_header_error) {
        *status = status_$ok;
    }

    /* Unwire the buffer */
    WP_$UNWIRE(wired_addr);
}
