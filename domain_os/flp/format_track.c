/*
 * FLP_FORMAT_TRACK - Format a track on the floppy disk
 *
 * This function formats a single track on a floppy disk by:
 * 1. Building a format buffer with sector ID fields (C, H, R, N)
 * 2. Seeking to the correct cylinder if needed
 * 3. Setting up DMA to send the format buffer
 * 4. Sending the format track command to the controller
 *
 * The format buffer contains 4 bytes per sector:
 *   C - Cylinder number
 *   H - Head number
 *   R - Sector number (1-based)
 *   N - Bytes per sector code (usually 2 for 512 bytes)
 */

#include "flp.h"

/* Status codes */
#define status_$disk_controller_busy  0x00080002

/* DMA controller base address */
#define DMA_BASE       0x00ffa000

/* DMA controller register offsets */
#define DMA_MODE       0xc5   /* Mode register */
#define DMA_CONTROL    0xc7   /* Control register */
#define DMA_COUNT      0xca   /* Transfer count */
#define DMA_ADDR       0xcc   /* Memory address */
#define DMA_START      0xe9   /* Start transfer */

/* DMA mode for write */
#define DMA_MODE_WRITE 0x12

/* FLP data area fields (relative to base 0xe7aef4) */
extern int32_t DAT_00e7b020;    /* +0x12c: Controller address */
extern uint16_t DAT_00e7b026;   /* +0x132: Retry counter */
extern uint32_t DAT_00e7aff0;   /* +0xfc: Physical address of format buffer */
extern uint8_t DAT_00e7af6c[];  /* +0x78: Unit cylinder cache */
extern uint16_t DAT_00e7af66;   /* +0x72: Status register copy */

/* Format buffer area at offset 0x80 from FLP data base */
extern uint8_t FLP_IO_BUFFER[]; /* 0xe7af74 */

/* Bytes per sector code at offset 0x103 */
extern uint8_t DAT_00e7aff7;    /* N value for format */

/* Format command buffer at offset 0x2c */
extern uint16_t DAT_00e7af20;   /* Command byte */
extern uint16_t DAT_00e7af22;   /* Unit + head */

/* Seek command buffer at offset 0x118 */
extern uint16_t DAT_00e7b00c;   /* Seek command */
extern uint16_t DAT_00e7b00e;   /* Unit + head */
extern uint16_t DAT_00e7b010;   /* Cylinder */

/* Controller info table */
extern uint8_t DAT_00e7afe0[];  /* +0xec: Controller address table */

/* Constant data */
extern uint8_t DAT_00e3ddc2[];  /* Seek command parameters */
extern uint8_t DAT_00e3ddc4[];  /* Format command parameters */

/* Error counter area */
extern uint8_t DAT_00e7a55c[];  /* Disk error counter */

/*
 * Request block structure (partial)
 *   +0x18: Pointer to disk info (controller number at offset 6)
 *   +0x1c: Unit number (word)
 *   +0x20: Sector count (word)
 *
 * Buffer structure (partial)
 *   +0x04: Cylinder number (word)
 *   +0x05: Track/cylinder high byte
 *   +0x06: Head number (byte)
 *   +0x0c: Status return (long)
 *   +0x1e: Error counter index (byte)
 */

/*
 * FLP_FORMAT_TRACK - Format a track
 *
 * @param req  Request block
 * @param buf  Buffer descriptor with format parameters
 */
void FLP_FORMAT_TRACK(void *req, void *buf)
{
    int32_t status;
    volatile flp_regs_t *regs;
    volatile uint8_t *dma;
    uint16_t sector_count;
    uint16_t unit;
    int16_t unit_cyl_offset;
    uint8_t cylinder;
    uint8_t head;
    uint8_t *fmt_buf;
    int success;

    /* Get controller address from controller table */
    void *disk_info = *(void **)((uint8_t *)req + 0x18);
    uint16_t ctlr_num = *(uint16_t *)((uint8_t *)disk_info + 6);
    uint32_t ctlr_offset = (uint32_t)ctlr_num * 8;
    DAT_00e7b020 = *(int32_t *)(&DAT_00e7afe0[ctlr_offset + 4]);
    regs = (volatile flp_regs_t *)(uintptr_t)DAT_00e7b020;

    /* Get sector count */
    sector_count = *(uint16_t *)((uint8_t *)req + 0x20);

    /* Build format buffer with sector IDs */
    if (sector_count != 0) {
        cylinder = *(uint8_t *)((uint8_t *)buf + 5);  /* Track number */
        head = *(uint8_t *)((uint8_t *)buf + 6);      /* Head number */

        fmt_buf = FLP_IO_BUFFER;
        for (uint16_t sec = 1; sec <= sector_count; sec++) {
            *fmt_buf++ = cylinder;       /* C - Cylinder */
            *fmt_buf++ = head;           /* H - Head */
            *fmt_buf++ = (uint8_t)sec;   /* R - Sector number (1-based) */
            *fmt_buf++ = DAT_00e7aff7;   /* N - Bytes per sector code */
        }
    }

    /* Clear retry counter */
    DAT_00e7b026 = 0;

    /* Check if controller is busy */
    if ((regs->status & FLP_STATUS_CMD_MASK) != 0) {
        status = status_$disk_controller_busy;
        goto set_error;
    }

    /* Get unit and head info */
    unit = *(uint16_t *)((uint8_t *)req + 0x1c);
    head = *(uint8_t *)((uint8_t *)buf + 6);

    /* Set up unit + head field */
    DAT_00e7af22 = unit + head * 4;

    status = 0;

    /* Check if we need to seek to a new cylinder */
    unit_cyl_offset = unit * 2;
    if (*(int16_t *)(&DAT_00e7af6c[unit_cyl_offset]) != *(int16_t *)((uint8_t *)buf + 4)) {
        /* Need to seek - set up seek command */
        DAT_00e7b00e = DAT_00e7af22;  /* Copy unit + head */
        DAT_00e7b010 = *(uint16_t *)((uint8_t *)buf + 4);  /* Cylinder */

        status = EXCS(&DAT_00e7b00c, DAT_00e3ddc2, req);

        /* Update cached cylinder */
        *(uint16_t *)(&DAT_00e7af6c[unit_cyl_offset]) = DAT_00e7af66;
    }

    if (status != status_$ok) {
        goto set_error;
    }

    /* Set control register */
    regs->control = 3;  /* Motor on, write direction */

    /* Configure DMA controller */
    dma = (volatile uint8_t *)(uintptr_t)DMA_BASE;

    /*
     * DMA count is (sector_count * 4) / 2 = sector_count * 2
     * Since each sector needs 4 bytes of ID info.
     */
    *(volatile uint16_t *)(dma + DMA_COUNT) = (uint16_t)((sector_count * 4) >> 1);
    *(volatile uint32_t *)(dma + DMA_ADDR) = DAT_00e7aff0;  /* Physical addr of format buffer */
    *(volatile uint8_t *)(dma + DMA_MODE) = DMA_MODE_WRITE;
    *(volatile uint8_t *)(dma + DMA_START) = 1;      /* Start DMA */
    *(volatile uint8_t *)(dma + DMA_CONTROL) = 0x80; /* Enable */

    /* Execute format track command */
    status = EXCS(&DAT_00e7af20, DAT_00e3ddc4, req);

    success = (status == status_$ok);
    if (success) {
        return;
    }

set_error:
    /* Clear error counter for this operation */
    uint8_t err_idx = *(uint8_t *)((uint8_t *)buf + 0x1e);
    DAT_00e7a55c[err_idx * 0x1c] = 0;

    /* Store error status in buffer */
    *(int32_t *)((uint8_t *)buf + 0x0c) = status;
}
