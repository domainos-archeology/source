/*
 * DISK_$AS_XFER_MULTI - Multiple asynchronous transfer operations
 *
 * Performs multiple asynchronous read or write operations on a volume.
 * This function handles buffer wiring, queue block allocation, and
 * result collection for batched I/O operations.
 *
 * @param vol_idx_ptr   Pointer to volume index
 * @param count_ptr     Pointer to transfer count
 * @param op_type_ptr   Pointer to operation type (0=read, 1=write)
 * @param daddr_array   Array of disk addresses
 * @param info_array    Array of pointers to info blocks (32 bytes each)
 * @param buffer_array  Array of buffer addresses (must be page-aligned)
 * @param status_array  Output: Array of status codes per transfer
 * @param status        Output: Overall status code
 *
 * TODO(source-pxn): PARTIALLY EMITTED.  DISK_$AS_XFER_MULTI is 694 bytes at
 * 0x00E6B962..0x00E6BC17.  What this file already reproduces: the buffer
 * alignment checks, the wiring loop, the CACHE_$FLUSH_VIRTUAL, the
 * DISK_$GET_QBLKS call, the DISK_$WRITE_MULTI / DISK_$READ_MULTI dispatch,
 * the unwire loop, the DISK_$RTN_QBLKS return and the abandoned-status fill.
 * What is missing is step 4/5/7 -- the per-transfer queue-block fill (see
 * the second TODO(source-pxn) below) -- which needs the queue block record
 * modelled beyond the few DISK_QBLK_* offsets in disk/disk_internal.h.
 */

#include "cache/cache.h"
#include "disk/disk_internal.h"
#include "mst/mst.h"
#include "wp/wp.h"

/* Status codes */
#define status_$disk_io_abandoned             0x00080029

/* Page alignment mask */
#define PAGE_ALIGN_MASK  0x3ff

void DISK_$AS_XFER_MULTI(uint16_t *vol_idx_ptr, int16_t *count_ptr,
                          int16_t *op_type_ptr, uint32_t *daddr_array,
                          uint32_t **info_array, uint32_t *buffer_array,
                          uint32_t *status_array, status_$t *status)
{
    uint16_t vol_idx;
    int16_t count;
    int16_t op_type;
    int16_t i;
    int16_t completed = 0;
    uint32_t wired_addrs[16];
    uint32_t local_daddr[16];
    uint32_t local_info[16][8];
    status_$t local_status[129];
    /*
     * A6-0x310 / A6-0x30C: the queue-block head and tail DISK_$GET_QBLKS
     * fills in (0x00E6BA7C).  Both are longword VALUES - 0x00E6BB10 /
     * 0x00E6BB7A push their contents, not their addresses.
     */
    uint32_t qblk_head = 0;
    uint32_t qblk_tail = 0;

    vol_idx = *vol_idx_ptr;
    count = *count_ptr;
    op_type = *op_type_ptr;

    /* Validate buffer alignments and copy parameters */
    for (i = 0; i < count; i++) {
        local_daddr[i] = daddr_array[i];

        /* Check page alignment */
        if ((buffer_array[i] & PAGE_ALIGN_MASK) != 0) {
            local_status[0] = status_$disk_buffer_not_page_aligned;
            goto cleanup;
        }

        /* Copy info for write operations */
        if (op_type == 1) {
            int16_t j;
            for (j = 0; j < 8; j++) {
                local_info[i][j] = info_array[i][j];
            }
        }
    }

    /* Wire all buffers */
    for (i = 0; i < count; i++) {
        wired_addrs[i] = MST_$WIRE(buffer_array[i], local_status);
        if (local_status[0] != status_$ok) {
            /* Unwire previously wired buffers */
            int16_t j;
            for (j = 0; j < i; j++) {
                WP_$UNWIRE(wired_addrs[j]);
            }
            goto cleanup;
        }
    }

    /* Flush cache for DMA coherency */
    CACHE_$FLUSH_VIRTUAL();

    /* Allocate queue blocks */
    DISK_$GET_QBLKS(count, &qblk_head, &qblk_tail);

    /*
     * TODO(source-pxn): NOT EMITTED.  The per-transfer queue-block fill of
     * DISK_$AS_XFER_MULTI (0x00E6B962, 694 bytes) is missing here: the walk
     * over the chain DISK_$GET_QBLKS just returned that stores each disk
     * address, buffer address and info-block pointer into its block, and,
     * for writes, copies the caller's 32-byte info block into it.  The
     * matching read-side copy-back after the transfer is missing too.  Both
     * need the queue block record modelled past the DISK_QBLK_* offsets that
     * disk/disk_internal.h has today.
     */

    /* Perform I/O */
    if (op_type == 1) {
        /* Write operation */
        /* 0x00E6BAF2 pushes the head value itself as the request list. */
        DISK_$WRITE_MULTI(0, (void *)(uintptr_t)qblk_head, local_status);
        completed = count;
    } else {
        /* Read operation */
        DISK_$READ_MULTI(vol_idx, 0, 0, qblk_head, qblk_tail, &completed,
                         local_status);
    }

    /* Unwire buffers and collect results */
    for (i = 0; i < count; i++) {
        WP_$UNWIRE(wired_addrs[i]);

        /* Copy info for read operations */
        if (op_type == 0) {
            int16_t j;
            for (j = 0; j < 8; j++) {
                info_array[i][j] = local_info[i][j];
            }
        }
    }

    /* Return queue blocks */
    DISK_$RTN_QBLKS(count, qblk_head, qblk_tail);

cleanup:
    /* Copy individual statuses */
    for (i = 0; i < count; i++) {
        status_array[i] = (i <= completed) ? local_status[i] : status_$disk_io_abandoned;
    }

    /* Mark abandoned transfers */
    for (i = completed + 1; i < count; i++) {
        status_array[i] = status_$disk_io_abandoned;
    }

    *status = local_status[0];
}
