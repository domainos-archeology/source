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

#include "disk.h"

/* Status codes */
#define status_$disk_buffer_not_page_aligned     0x00080013
#define status_$disk_block_header_error          0x00080011
#define status_$volume_in_use                    0x0008000b
#define status_$operation_requires_physical_vol  0x0008000e

/* Volume table offsets */
#define DISK_LV_DATA_OFFSET       0x84
#define DISK_MOUNT_STATE_OFFSET   0x90
#define DISK_MOUNT_PROC_OFFSET    0x92
#define DISK_ADDR_START_OFFSET    0x88
#define DISK_ADDR_END_OFFSET      0x8c

/* Volume table base */
#define DISK_VOLUME_BASE  ((uint8_t *)0x00e7a1cc)

/* Valid volume index mask (volumes 1-10) */
#define VALID_VOL_MASK  0x7fe

/* Page alignment mask */
#define PAGE_ALIGN_MASK  0x3ff

/* Mount state 2 = assigned */
#define DISK_MOUNT_ASSIGNED  2

/* Current process ID */
extern int16_t PROC1_$CURRENT;

/* Global diagnostic mode flag at 0xe7acca */
extern int8_t DISK_$DIAG;

/* External functions */
extern uint32_t MST_$WIRE(uint32_t buffer, status_$t *status);
extern void WP_$UNWIRE(uint32_t wired_addr);
extern uint32_t WP_$CALLOC(void *addr_ptr, status_$t *status);
extern void MMAP_$FREE(uint32_t addr);
extern void CACHE_$FLUSH_VIRTUAL(void);
extern int8_t ACL_$IS_SUSER(void);
extern status_$t DISK_IO(int16_t op, int16_t vol_idx, void *buffer,
                         void *daddr, void *info);

void DISK_$DIAG_IO(int16_t *op_ptr, uint16_t *vol_idx_ptr, uint32_t *daddr_ptr,
                   void *buffer, uint32_t *info, status_$t *status)
{
    int16_t op;
    uint16_t vol_idx;
    uint32_t daddr;
    int32_t offset;
    uint8_t *vol_entry;
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

    offset = (int16_t)(vol_idx * DISK_VOLUME_SIZE);
    vol_entry = DISK_VOLUME_BASE + offset;

    /* Must be a physical volume (no LV data) */
    if (*(uint32_t *)(vol_entry + DISK_LV_DATA_OFFSET) != 0) {
        *status = status_$operation_requires_physical_vol;
        return;
    }

    /* Check access permissions */
    access_granted = 0;
    direct_access = 0;

    mount_state = *(uint16_t *)(vol_entry + DISK_MOUNT_STATE_OFFSET);
    mount_proc = *(int16_t *)(vol_entry + DISK_MOUNT_PROC_OFFSET);

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
        addr_start = *(uint32_t *)(vol_entry + DISK_ADDR_START_OFFSET);
        addr_end = *(uint32_t *)(vol_entry + DISK_ADDR_END_OFFSET);
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
            wired_addr = WP_$CALLOC(&wired_addr, status);
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
    io_status = DISK_IO(io_op, vol_idx, (void *)(uintptr_t)wired_addr,
                        (void *)(uintptr_t)daddr, local_info);
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
