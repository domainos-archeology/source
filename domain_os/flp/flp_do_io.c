/*
 * FLP_DO_IO - Core floppy disk I/O handler
 *
 * This function performs the actual I/O operations for floppy disk access.
 * It handles:
 * - Read operations (type 1)
 * - Write operations (type 2)
 * - Format operations (type 3) - delegated to FLP_FORMAT_TRACK
 *
 * The function sets up DMA transfers and sends appropriate commands
 * to the floppy controller via EXCS. It includes retry logic and
 * handles seek operations when the head position changes.
 */

#include "flp/flp_internal.h"

/* Status codes */
#define status_$disk_controller_busy    0x00080002
#define status_$storage_module_stopped  0x0008001b

/* Retry marker */
#define FLP_RETRY_NEEDED               0x0008ffff

/* I/O operation types */
#define FLP_OP_READ     1
#define FLP_OP_WRITE    2
#define FLP_OP_FORMAT   3

/* FLP data area base */
#define FLP_DATA_BASE  0x00e7aef4

/* DMA controller base address */
#define DMA_BASE       0x00ffa000

/* DMA controller register offsets */
#define DMA_MODE       0xc5   /* Mode register */
#define DMA_CONTROL    0xc7   /* Control register */
#define DMA_COUNT      0xca   /* Transfer count */
#define DMA_ADDR       0xcc   /* Memory address */
#define DMA_START      0xe9   /* Start transfer */

/* DMA mode values */
#define DMA_MODE_READ  0x92   /* Read from device to memory */
#define DMA_MODE_WRITE 0x12   /* Write from memory to device */

/*
 * Request block structure (partial)
 *   +0x18: Pointer to disk info (controller number at offset 6)
 *   +0x1c: Unit number (word)
 *
 * Buffer structure (partial)
 *   +0x04: Cylinder number (word)
 *   +0x06: Head number (byte)
 *   +0x07: Starting sector (byte)
 *   +0x0c: Status return (long)
 *   +0x14: Physical buffer address (long)
 *   +0x1e: Error counter index (byte)
 *   +0x1f: Operation type (low nibble: 1=read, 2=write, 3=format)
 */

/*
 * FLP_DO_IO - Perform floppy disk I/O
 *
 * @param req       Request block
 * @param buf       I/O buffer descriptor
 * @param param_3   Unused parameter
 * @param lba_hi    High word of LBA (unused for floppy)
 * @param lba_lo    Low word of LBA (unused for floppy)
 */
void FLP_DO_IO(void *req, void *buf, void *param_3, uint16_t lba_hi, uint32_t lba_lo)
{
    int32_t status;
    int32_t ctlr_table_offset;
    uint16_t ctlr_num;
    int16_t sectors_done;
    int16_t lock_id;
    uint8_t op_type;
    int16_t unit;
    int16_t unit_cyl_offset;
    uint8_t *result_ptr;
    volatile flp_regs_t *regs;
    volatile uint8_t *dma;
    void *ctlr_info;
    int success;

    /* Get controller number from request block */
    void *disk_info = *(void **)((uint8_t *)req + 0x18);
    ctlr_num = *(uint16_t *)((uint8_t *)disk_info + 6);

    /* Calculate controller table offset */
    ctlr_table_offset = (uint32_t)ctlr_num * 8;

    /* Get controller register address */
    DAT_00e7b020 = *(int32_t *)(&DAT_00e7afe0[ctlr_table_offset + 4]);
    regs = (volatile flp_regs_t *)(uintptr_t)DAT_00e7b020;

    /* Clear output/result pointer */
    result_ptr = (uint8_t *)((uintptr_t)((uint32_t)lba_hi << 16) | (lba_lo & 0xFFFF));
    if (result_ptr != NULL) {
        *result_ptr = 0;
    }

    /* Acquire lock */
    ctlr_info = *(void **)(&DAT_00e7afdc[ctlr_table_offset]);
    lock_id = *(int16_t *)((uint8_t *)ctlr_info + 0x3c);
    ML_$LOCK(lock_id);

    /* Get operation type */
    op_type = *(uint8_t *)((uint8_t *)buf + 0x1f) & 0x0f;

    if (op_type == FLP_OP_FORMAT) {
        /* Format operation - delegate to format function */
        FLP_FORMAT_TRACK(req, buf);
        goto unlock_and_return;
    }

    /* Check for write to changed disk */
    unit = *(int16_t *)((uint8_t *)req + 0x1c);
    if (op_type == FLP_OP_WRITE && (int8_t)DAT_00e7b018[unit] < 0) {
        status = status_$storage_module_stopped;
        success = 0;
        goto set_error;
    }

    /* Initialize retry counters */
    DAT_00e7b026 = 25;   /* Command retry count */
    DAT_00e7b024 = 500;  /* DMA retry count */

    /* Store physical buffer address for DMA */
    DAT_00e7b01c = *(uint32_t *)((uint8_t *)buf + 0x14);

    sectors_done = 0;
    dma = (volatile uint8_t *)(uintptr_t)DMA_BASE;

    do {
        /* Check if controller is busy */
        if ((regs->status & FLP_STATUS_CMD_MASK) != 0) {
            status = status_$disk_controller_busy;
            success = 0;
            goto set_error;
        }

        /*
         * Set up command parameters:
         *   DAT_00e7af44 = head number
         *   DAT_00e7af40 = (head * 4) + unit
         *   DAT_00e7af42 = cylinder
         *   DAT_00e7af46 = sector number
         */
        uint8_t head = *(uint8_t *)((uint8_t *)buf + 0x06);
        DAT_00e7af44 = (uint16_t)head;
        DAT_00e7af40 = unit + head * 4;
        DAT_00e7af42 = *(uint16_t *)((uint8_t *)buf + 0x04);  /* Cylinder */
        DAT_00e7af46 = sectors_done + *(uint8_t *)((uint8_t *)buf + 0x07) + 1;  /* Sector */

        status = 0;

        /* Check if we need to seek to a new cylinder */
        unit_cyl_offset = unit * 2;
        if (*(int16_t *)(&DAT_00e7af6c[unit_cyl_offset]) != *(int16_t *)((uint8_t *)buf + 0x04)) {
            /* Need to seek - send recalibrate/seek command (0x0f) */
            DAT_00e7af3e = 0x0f;  /* Seek command */
            status = EXCS(&DAT_00e7af3e, DAT_00e3ddc2, req);
            /* Update cached cylinder */
            *(uint16_t *)(&DAT_00e7af6c[unit_cyl_offset]) = DAT_00e7af66;
        }

        if (status == status_$ok) {
            /* Set up control register for direction */
            if (op_type == FLP_OP_READ) {
                regs->control = 2;  /* Motor on, read direction */
            } else {
                regs->control = 3;  /* Motor on, write direction */
            }

            /* Configure DMA controller */
            *(volatile uint16_t *)(dma + DMA_COUNT) = 0x200;  /* 512 bytes */
            *(volatile uint32_t *)(dma + DMA_ADDR) = *(uint32_t *)((uint8_t *)buf + 0x14) << 10;

            if (op_type == FLP_OP_WRITE) {
                *(volatile uint8_t *)(dma + DMA_MODE) = DMA_MODE_WRITE;
            } else {
                *(volatile uint8_t *)(dma + DMA_MODE) = DMA_MODE_READ;
            }

            *(volatile uint8_t *)(dma + DMA_START) = 1;      /* Start DMA */
            *(volatile uint8_t *)(dma + DMA_CONTROL) = 0x80; /* Enable */

            /* Set up read/write command */
            if (op_type == FLP_OP_READ) {
                DAT_00e7af3e = DAT_00e7affa + 6;  /* Read command */
            } else {
                DAT_00e7af3e = DAT_00e7affa + 5;  /* Write command */
            }

            /* Execute the command */
            status = EXCS(&DAT_00e7af3e, DAT_00e3dfe0, req);

            if (status == status_$ok) {
                sectors_done++;
            }
        }

        /* If no sectors transferred and no error, mark for retry */
        if (sectors_done == 0 && status == status_$ok) {
            status = FLP_RETRY_NEEDED;
        }

    } while (status == FLP_RETRY_NEEDED);

    /* Check for disk change after successful operation */
    if (status == status_$ok && (int8_t)DAT_00e7b018[unit] < 0) {
        status = status_$storage_module_stopped;
    }

    /* Restore control register */
    regs->control = 3;

    success = (status == status_$ok);

set_error:
    if (!success) {
        /* Clear error counter for this operation */
        uint8_t err_idx = *(uint8_t *)((uint8_t *)buf + 0x1e);
        /* Calculate index: (err_idx * 0x1c) offset into error counter array */
        DAT_00e7a55c[err_idx * 0x1c] = 0;

        /* Store error status in buffer */
        *(int32_t *)((uint8_t *)buf + 0x0c) = status;
    }

unlock_and_return:
    /* Release lock */
    ctlr_info = *(void **)(&DAT_00e7afdc[ctlr_table_offset]);
    lock_id = *(int16_t *)((uint8_t *)ctlr_info + 0x3c);
    ML_$UNLOCK(lock_id);
}
