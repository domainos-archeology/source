/*
 * smd/blt.c - SMD_$BLT implementation
 *
 * Performs a bit block transfer operation.
 *
 * Original address: 0x00E6EC6E
 *
 * Assembly analysis:
 * This is a complex function that:
 * 1. Validates the current process has an associated display
 * 2. Validates the BLT parameters (mode bits)
 * 3. Converts user-facing BLT parameters to hardware format
 * 4. Acquires display lock (sync or async depending on mode)
 * 5. Starts the BLT operation
 * 6. Releases lock if sync mode
 *
 * Error codes:
 *   0x130004 - invalid use of driver procedure (no display)
 *   0x130028 - invalid BLT operation (bad mode bits)
 */

#include "smd/smd_internal.h"

/*
 * Hardware BLT parameter structure
 * This is the internal format passed to SMD_$START_BLT
 */
typedef struct smd_hw_blt_t {
    uint16_t    control;        /* 0x00: Control word */
    uint16_t    bit_pos;        /* 0x02: Bit position */
    uint16_t    mask;           /* 0x04: Mask */
    uint16_t    pattern;        /* 0x06: Pattern/ROP */
    uint16_t    y_extent;       /* 0x08: Y extent (negative: height-1) */
    uint16_t    x_extent;       /* 0x0A: X extent (negative: width-1) */
    uint16_t    y_start;        /* 0x0C: Y start coordinate */
    uint16_t    x_start;        /* 0x0E: X start coordinate */
} smd_hw_blt_t;

/* Lock data for async vs sync BLT */
extern int16_t SMD_BLT_ASYNC_LOCK_DATA;  /* at 0x00E6D92C */
extern int16_t SMD_BLT_SYNC_LOCK_DATA;   /* at 0x00E6DFF8 */

/*
 * SMD_$BLT - Bit block transfer
 *
 * Performs a hardware-accelerated bit block transfer.
 *
 * Parameters:
 *   params     - User BLT parameters (see assembly for format)
 *   param2     - Reserved (unused)
 *   param3     - Reserved (unused)
 *   status_ret - Output: status return
 *
 * BLT mode bits:
 *   bit 7: direction (must be 0)
 *   bit 6: invalid operation flag (must be 0)
 *   bit 5: use alternate ROP
 *   bit 4: async operation (use interrupts)
 *   bit 3: invalid operation flag (must be 0)
 *
 * Returns:
 *   status_$ok on success
 *   status_$display_invalid_use_of_driver_procedure if no display
 *   status_$display_invalid_blt_op if mode bits invalid
 */
void SMD_$BLT(uint16_t *params, uint32_t param2, uint32_t param3, status_$t *status_ret)
{
    uint16_t unit;
    int32_t unit_offset;
    smd_display_hw_t *hw;
    smd_display_unit_t *display_unit;
    uint16_t mode;
    int16_t *lock_data;
    smd_hw_blt_t hw_params;
    int16_t dx, dy;

    (void)param2;
    (void)param3;

    /* Get current process's display unit */
    unit = SMD_GLOBALS.asid_to_unit[PROC1_$AS_ID];

    if (unit == 0) {
        *status_ret = status_$display_invalid_use_of_driver_procedure;
        return;
    }

    mode = params[0];

    /* Calculate unit offset */
    unit_offset = (int32_t)unit * SMD_DISPLAY_UNIT_SIZE;
    display_unit = (smd_display_unit_t *)((uint8_t *)&SMD_EC_1 + unit_offset);
    hw = (smd_display_hw_t *)((uint8_t *)SMD_UNIT_AUX_BASE + unit_offset);

    /* Select lock data based on async mode */
    if ((mode & 0x10) != 0) {
        lock_data = &SMD_BLT_ASYNC_LOCK_DATA;
    } else {
        lock_data = &SMD_BLT_SYNC_LOCK_DATA;
    }

    /* Acquire display lock */
    SMD_$ACQ_DISPLAY(lock_data);

    /* Validate mode bits: bit 7, bit 6, and bit 3 must be clear */
    if ((int8_t)mode < 0 || (mode & 0x40) != 0 || (mode & 0x08) != 0) {
        *status_ret = status_$display_invalid_blt_op;
        SMD_$REL_DISPLAY();
        return;
    }

    /* Build hardware BLT parameters */

    /* Control byte: build from mode flags */
    hw_params.control = ((mode & 0x8000) ? 0x80 : 0) |      /* Direction */
                        ((mode & 0x20) ? 0x20 : 0) |        /* Alt ROP */
                        ((mode & 0x10) ? 0x10 : 0) |        /* Async */
                        ((((uint8_t *)&params[1])[3] == 0x02) ? 0x08 : 0) |  /* Pattern type */
                        ((((uint8_t *)&params[1])[0] == 0x20) ? 0x04 : 0) |  /* Mask type */
                        ((mode & 0x02) ? 0x02 : 0) |        /* Src enable */
                        ((mode & 0x01) ? 0x01 : 0);         /* Dest enable */

    /* Bit position: plane select from low nibble of params[12] */
    hw_params.bit_pos = params[12] & 0x0F;

    /* Pattern and mask from params[5-6] */
    hw_params.pattern = params[5];
    hw_params.mask = params[6];

    /* Calculate extents (negative values) */
    dy = params[11] - params[7];
    if (dy < 0) dy = -dy;
    hw_params.y_extent = -1 - dy;

    dx = (params[12] >> 4) - (params[8] >> 4);
    if (dx < 0) dx = -dx;
    hw_params.x_extent = -1 - dx;

    /* Start coordinates */
    hw_params.y_start = params[7];
    hw_params.x_start = params[8];

    /* Start the BLT operation */
    SMD_$START_BLT((uint16_t *)&hw_params, hw,
                   (uint16_t *)((uint8_t *)&SMD_EC_1 + unit_offset + 8));

    if ((mode & 0x10) == 0) {
        /* Sync mode - release display lock now */
        SMD_$REL_DISPLAY();
    } else {
        /* Async mode - record owner ASID for later release */
        display_unit->asid = PROC1_$AS_ID;
    }

    *status_ret = status_$ok;
}
