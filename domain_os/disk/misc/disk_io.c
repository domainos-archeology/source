/*
 * DISK_IO - Main disk I/O entry point
 *
 * Performs disk read, write, and format operations. Handles write
 * protection, exclusion locking, error recovery, and optional
 * read-after-write verification for data integrity.
 *
 * From: 0x00e3d50e
 *
 * Parameters:
 *   op         - Operation type:
 *                  0 = read (cached)
 *                  1 = read (direct)
 *                  2 = write (cached)
 *                  3 = write (direct)
 *                  4 = format
 *   vol_idx    - Volume index (0-10)
 *   daddr      - Disk address (block number)
 *   buffer     - Buffer physical page number
 *   info       - I/O info array (8 longwords for block header data)
 *
 * Returns:
 *   status_$ok on success
 *   status_$invalid_volume_index if vol_idx > 10
 *   status_$disk_write_protected if write to protected volume
 *   Various disk error codes on failure
 *
 * Block header info array layout (8 longwords):
 *   [0]: UID high
 *   [1]: UID low
 *   [2]: Block checksum/validation
 *   [3-7]: Additional header data
 */

#include "disk/disk_internal.h"
#include "mmu/mmu.h"
#include "time/time.h"
#include "proc1/proc1.h"
#include "netlog/netlog.h"
#include "misc/crash_system.h"

/* Additional disk status codes */
#define status_$disk_block_header_error         0x00080011
#define status_$software_detected_checksum_error 0x0008001F
#define status_$checksum_error_in_read_after_write 0x00080020
#define status_$read_after_write_failed         0x0008001C

/* Operation types */
#define DISK_OP_READ_CACHED     0
#define DISK_OP_READ_DIRECT     1
#define DISK_OP_WRITE_CACHED    2
#define DISK_OP_WRITE_DIRECT    3
#define DISK_OP_FORMAT          4

/* Internal operation codes used for DISK_$DO_IO */
#define DISK_INTERNAL_OP_READ   1
#define DISK_INTERNAL_OP_WRITE  2
#define DISK_INTERNAL_OP_FORMAT 9

/* Volume flags at offset 0xa5 */
#define VOL_FLAG_WRITE_PROTECTED    0x01
#define VOL_FLAG_CHECKSUM_ENABLED   0x02
#define VOL_FLAG_RAW_VERIFY         0x04

/* Volume info structure flags at offset 0x08 */
#define VOL_INFO_FLAG_LOGGING       0x0200
#define VOL_INFO_FLAG_CHECKSUM      0x4000

/* Disk data base offsets */
#if defined(M68K)
#define DISK_DATA_BASE      ((uint8_t *)0xe7a1cc)
#define DISK_RAW_PPN        (*(uint32_t *)0xe7acb8)  /* +0xAEC from base */
#else
extern uint8_t *disk_data_base;
extern uint32_t disk_raw_ppn;
#define DISK_DATA_BASE      disk_data_base
#define DISK_RAW_PPN        disk_raw_ppn
#endif

/* Volume entry size and offset calculations */
#define VOL_ENTRY_SIZE      0x48
#define VOL_ENTRY(idx)      (DISK_DATA_BASE + 0x7C + (idx) * VOL_ENTRY_SIZE)
#define VOL_FLAGS(idx)      (*(uint8_t *)(VOL_ENTRY(idx) + 0x29))  /* 0xa5 - 0x7c */
#define VOL_INFO_PTR(idx)   (*(void **)(VOL_ENTRY(idx) + 0x18))    /* 0x94 - 0x7c */

/* I/O request structure (used internally) */
typedef struct disk_io_req_t {
    uint32_t reserved1;         /* +0x00 */
    uint32_t daddr;             /* +0x04: Disk address */
    uint8_t head;               /* +0x06: Head number (for format) */
    uint8_t sector;             /* +0x07: Sector number (for format) */
    uint32_t reserved2;         /* +0x08 */
    status_$t status;           /* +0x0C: Result status */
    uint32_t reserved3;         /* +0x10 */
    uint32_t ppn;               /* +0x14: Physical page number */
    uint16_t reserved4;         /* +0x18 */
    uint16_t count;             /* +0x1A: Transfer count */
    uint8_t reserved5;          /* +0x1C */
    uint8_t reserved6;          /* +0x1D */
    uint8_t reserved7;          /* +0x1E */
    uint8_t flags;              /* +0x1F: Request flags */
    uint32_t header[8];         /* +0x20: Block header data */
    uint32_t timestamp;         /* +0x2C: Timestamp (for writes) */
    uint32_t reserved8[2];      /* +0x30, +0x34 */
    uint16_t checksum;          /* +0x3A: Checksum */
} disk_io_req_t;

/* External helper functions */
extern void FUN_00e3be8a(int16_t count, int8_t mode, int32_t *req_out, uint32_t *param2);
extern void FUN_00e3c01a(int16_t count, void *req, uint32_t param);
extern void FUN_00e3cae0(void *req, uint16_t vol_idx, int16_t op, void *param1, status_$t *status);
extern void FUN_00e3c9fe(int16_t mask, int32_t *counter1, int32_t *counter2);
extern void FUN_00e3c14c(int16_t vol_idx, void *req, int32_t *info);
extern uint16_t FUN_00e0a290(void *addr);  /* Checksum calculation */

/* Netlog function */
extern int8_t NETLOG_$OK_TO_LOG;
extern void NETLOG_$LOG_IT(uint16_t type, void *header, uint16_t p1, uint16_t p2,
                           uint16_t p3, uint16_t p4, uint16_t p5, uint16_t p6);

status_$t DISK_IO(uint16_t op, uint16_t vol_idx, uint32_t daddr,
                  uint32_t ppn, int32_t *info)
{
    disk_io_req_t *req;
    int32_t req_ptr;
    uint32_t req_param;
    int16_t internal_op;
    uint8_t vol_flags;
    uint8_t need_exclusion;
    uint8_t do_header_check;
    uint8_t do_checksum;
    int32_t local_counters[2];
    int32_t verify_info[8];
    char io_result;
    void *vol_info;
    int16_t i;
    status_$t result_status;

    /* Validate volume index */
    if (vol_idx > 10) {
        return status_$invalid_volume_index;
    }

    /* Clear local I/O parameter area */
    for (i = 0; i < 10; i++) {
        ((int32_t *)&op)[i * 2 - 0x30] = 0;  /* Match original stack clearing */
    }

    /* Get volume info structure pointer */
    vol_info = VOL_INFO_PTR(vol_idx);

    /* Determine internal operation code */
    switch (op) {
    case DISK_OP_READ_CACHED:
    case DISK_OP_WRITE_CACHED:
        internal_op = DISK_INTERNAL_OP_READ;
        break;
    case DISK_OP_READ_DIRECT:
    case DISK_OP_WRITE_DIRECT:
        internal_op = DISK_INTERNAL_OP_WRITE;
        /* Check write protection */
        if ((VOL_FLAGS(vol_idx) & VOL_FLAG_WRITE_PROTECTED) != 0) {
            return status_$disk_write_protected;
        }
        break;
    case DISK_OP_FORMAT:
        internal_op = DISK_INTERNAL_OP_FORMAT;
        break;
    default:
        internal_op = DISK_INTERNAL_OP_READ;
        break;
    }

    /* Determine if header/checksum verification is needed */
    do_header_check = (op != DISK_OP_FORMAT && op != DISK_OP_WRITE_CACHED &&
                       *(int16_t *)((uint8_t *)vol_info + 8) >= 0) ? 0xFF : 0;
    do_checksum = (do_header_check &&
                   (*(uint16_t *)((uint8_t *)vol_info + 8) & VOL_INFO_FLAG_CHECKSUM) != 0) ? 0xFF : 0;

    /* Allocate I/O request structure */
    FUN_00e3be8a(1, 0xFF, &req_ptr, &req_param);
    req = (disk_io_req_t *)req_ptr;

    /* Copy block header info to request */
    for (i = 0; i < 8; i++) {
        req->header[i] = info[i];
    }

    /* Set up request parameters */
    req->daddr = daddr;

    /* Initialize request via helper */
    FUN_00e3cae0(req, vol_idx, internal_op, (void *)((uint8_t *)&op - 0x58), &req->status);

    if (req->status != status_$ok) {
        goto cleanup;
    }

    /* For format operations, extract head/sector from daddr */
    if (op == DISK_OP_FORMAT) {
        req->head = (uint8_t)daddr;
        req->sector = 0;
        req->daddr = 0;
    }

    /* Set physical page number */
    req->ppn = ppn;

    /* Set checksum flag in request */
    vol_flags = VOL_FLAGS(vol_idx);
    req->flags &= 0x7F;
    if ((vol_flags & VOL_FLAG_CHECKSUM_ENABLED) != 0) {
        req->flags |= 0x80;
    }

    /* Acquire exclusion lock if needed for checksummed writes */
    need_exclusion = 0;
    if (do_checksum < 0) {  /* Signed comparison - 0xFF is negative */
        ML_$EXCLUSION_START(&ml_$exclusion_t_00e7a274);
        need_exclusion = 0xFF;
    }

    /* For writes with verification, set up timestamp and checksum */
    if (internal_op == DISK_INTERNAL_OP_WRITE &&
        *(int16_t *)((uint8_t *)vol_info + 8) >= 0) {
        uint8_t verify_flags = (op == DISK_OP_WRITE_DIRECT) ? 0xFF : 0;
        verify_flags |= ((vol_flags & VOL_FLAG_RAW_VERIFY) != 0 &&
                         op == DISK_OP_READ_DIRECT) ? 0xFF : 0;

        if ((int8_t)verify_flags >= 0) {
            req->timestamp = TIME_$CLOCKH;
            req->header[4] = 0;  /* Clear reserved fields */
            req->header[5] = 0;

            if (do_checksum < 0) {
                req->checksum = FUN_00e0a290(&req->ppn);
            } else {
                req->checksum = 0;
            }
        }
    }

    /* For reads, invert checksum field for validation */
    if (internal_op == DISK_INTERNAL_OP_READ) {
        req->header[2] = ~req->header[2];
    }

    /* Update MMU mapping */
    MMU_$MCR_CHANGE(6);

    /* Save event counters for wait */
    local_counters[0] = *(int32_t *)(DISK_DATA_BASE + 0x378 +
                                      (PROC1_$CURRENT * 0x1C)) + 1;
    local_counters[1] = *(int32_t *)(DISK_DATA_BASE + 0x384 +
                                      (PROC1_$CURRENT * 0x1C)) + 1;

    /* Perform the actual I/O */
    DISK_$DO_IO(VOL_ENTRY(vol_idx) + 0x7C, req, req, &io_result);

    /* If async I/O, wait for completion */
    if ((int8_t)io_result < 0) {
        FUN_00e3c9fe((int16_t)(1 << vol_idx), &local_counters[0], &local_counters[1]);
    }

    /* Handle I/O errors */
    if (req->status != status_$ok) {
        if (req->status == status_$disk_write_protected) {
            VOL_FLAGS(vol_idx) |= VOL_FLAG_WRITE_PROTECTED;
            goto cleanup;
        }

        /* Call error handler */
        FUN_00e3c14c(vol_idx, req, info);

        /* Check for recoverable errors */
        if (req->status != 0x80031 &&
            req->status != 0x8002F &&
            req->status != 0x80030) {
            goto cleanup;
        }

        /* Clear status for retry */
        req->status = status_$ok;
    }

    /* Verify block header for reads */
    if (do_header_check && internal_op == DISK_INTERNAL_OP_READ) {
        if (info[0] != req->header[0] ||
            info[1] != req->header[1] ||
            info[2] != req->header[2]) {
            req->status = status_$disk_block_header_error;
            FUN_00e3c14c(vol_idx, req, info);
            goto cleanup;
        }

        /* Verify checksum if enabled */
        if (do_checksum < 0) {
            uint16_t calc_checksum = FUN_00e0a290(&req->ppn);
            if (calc_checksum != req->checksum && req->checksum != 0) {
                req->status = status_$software_detected_checksum_error;
                CRASH_SYSTEM(&req->status);
            }
        }
    }

    /* Read-after-write verification for writes */
    if (do_checksum < 0 && internal_op == DISK_INTERNAL_OP_WRITE && DISK_RAW_PPN != 0) {
        status_$t verify_status;

        /* Read back the block */
        verify_status = DISK_IO(DISK_OP_WRITE_CACHED, vol_idx, DISK_RAW_PPN,
                                daddr, verify_info);

        if (verify_status != status_$ok) {
            req->status = status_$read_after_write_failed;
            CRASH_SYSTEM(&req->status);
        }

        /* Compare headers */
        for (i = 0; i < 8; i++) {
            if (verify_info[i] != req->header[i]) {
                req->status = status_$read_after_write_failed;
                CRASH_SYSTEM(&req->status);
            }
        }

        /* Verify checksum */
        {
            uint16_t verify_checksum = FUN_00e0a290((void *)0xACB8);  /* Raw buffer */
            if (verify_checksum != req->checksum) {
                req->status = status_$checksum_error_in_read_after_write;
                CRASH_SYSTEM(&req->status);
            }
        }
    }

    /* Log the I/O operation if logging enabled */
    if (NETLOG_$OK_TO_LOG < 0) {
        uint16_t log_type = (internal_op == DISK_INTERNAL_OP_READ) ? 6 : 7;
        uint16_t log_flags = *(uint16_t *)((uint8_t *)vol_info + 8);

        if ((log_flags & VOL_INFO_FLAG_LOGGING) == 0) {
            NETLOG_$LOG_IT(log_type, req->header,
                           (uint16_t)(req->header[2] >> 5),
                           (uint16_t)(req->header[2] & 0x1F),
                           req->count,
                           *(uint16_t *)&req->daddr,
                           req->head,
                           req->sector);
        } else {
            NETLOG_$LOG_IT(log_type, req->header, 0, 0, req->count, 0, 0, 0);
        }
    }

cleanup:
    /* Release exclusion lock if held */
    if ((int8_t)need_exclusion < 0) {
        ML_$EXCLUSION_STOP(&ml_$exclusion_t_00e7a274);
    }

    /* Copy result header back for reads */
    if (internal_op == DISK_INTERNAL_OP_READ) {
        for (i = 0; i < 8; i++) {
            info[i] = req->header[i];
        }
    }

    /* Save status and free request */
    result_status = req->status;
    FUN_00e3c01a(1, req, req_param);

    return result_status;
}
