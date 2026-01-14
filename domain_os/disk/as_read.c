/*
 * DISK_$AS_READ - Asynchronous sector read
 *
 * Performs an async read operation, returning extended status info.
 *
 * @param vol_idx_ptr  Pointer to volume index
 * @param daddr_ptr    Pointer to disk address
 * @param count_ptr    Pointer to sector count
 * @param info         Output: Extended I/O info (32 bytes)
 * @param status       Output: Status code
 */

#include "disk.h"

/* Block header error status */
#define status_$disk_block_header_error  0x00080011

/* External functions */
extern uint32_t AS_IO_SETUP(uint16_t *vol_idx_ptr, uint32_t buffer, status_$t *status);
extern void WP_$UNWIRE(uint32_t wired_addr);
extern status_$t DISK_IO(int16_t op, int16_t vol_idx, void *buffer,
                         void *daddr, void *info);

void DISK_$AS_READ(uint16_t *vol_idx_ptr, uint32_t *daddr_ptr, uint16_t *count_ptr,
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
    wired_addr = AS_IO_SETUP(&vol_idx, (uint32_t)(uintptr_t)count_ptr, status);

    if ((int16_t)((*status >> 16) & 0xFFFF) != 0) {
        return;
    }

    /* The count pointer is used as the buffer address in this context */
    *count_ptr = *count_ptr;  /* Touch the count */

    /* Initialize info */
    local_info[0] = 0;

    /* Perform the read (op=2 for read) */
    io_status = DISK_IO(2, vol_idx, (void *)(uintptr_t)wired_addr,
                        (void *)(uintptr_t)daddr, local_info);
    *status = io_status;

    /* Copy extended info to caller */
    for (i = 0; i < 8; i++) {
        info[i] = local_info[i];
    }

    /* Block header error is OK for this operation */
    if (io_status == status_$disk_block_header_error) {
        *status = status_$ok;
    }

    /* Unwire the buffer */
    WP_$UNWIRE(wired_addr);
}
