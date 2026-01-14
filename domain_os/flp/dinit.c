/*
 * FLP_$DINIT - Floppy device (unit) initialization
 *
 * This function initializes a specific floppy drive unit. It:
 * 1. Validates the unit number
 * 2. Sets up DMA buffer for the unit
 * 3. Recalibrates the drive
 * 4. Returns drive geometry information
 *
 * The function also wires the I/O buffer into physical memory
 * on first initialization to ensure DMA can access it.
 */

#include "flp.h"

/* Controller table entry offset to register address */
extern uint8_t DAT_00e7afe0[];

/* Current controller register address */
extern int32_t DAT_00e7b020;

/* Physical address of I/O buffer */
extern uint32_t DAT_00e7aff0;

/* Initialized flag */
extern int8_t DAT_00e7b02c;

/* Unit status */
extern uint8_t DAT_00e7af6c[];  /* 2 bytes per unit */
extern uint8_t DAT_00e7b018[];  /* Disk change flags */

/* Control data */
extern uint8_t DAT_00e7b008[];  /* EXCS data area */
extern uint16_t DAT_00e7b00a;   /* Current unit number */
extern uint16_t DAT_00e7b026;   /* Control flag */

/* I/O buffer area at offset 0x80 from FLP data base */
extern uint8_t FLP_IO_BUFFER[];  /* 0xe7af74 */

/* Geometry data embedded in code */
extern uint32_t DAT_00e3e21e;    /* Geometry word 1 */
extern uint32_t DAT_00e3e222;    /* Geometry word 2 */
extern uint16_t DAT_00e3e226;    /* Geometry word 3 */

/* Command signature */
extern uint8_t DAT_00e3e21c[];

/*
 * FLP_$DINIT - Initialize floppy device
 *
 * @param unit      Unit number (0-3)
 * @param ctlr      Controller number
 * @param params    I/O: Disk parameters (cylinders if > 0 on input)
 * @param heads     Output: Number of heads
 * @param sectors   Output: Sectors per track
 * @param geometry  Output: Geometry information (3 words)
 * @param flags     Output: Drive flags
 * @return Status code
 */
status_$t FLP_$DINIT(uint16_t unit, uint16_t ctlr,
                      int32_t *params, uint16_t *heads,
                      uint16_t *sectors, uint32_t *geometry,
                      uint16_t *flags)
{
    status_$t status;
    uint32_t phys_addr;
    status_$t vtop_status;
    volatile flp_regs_t *regs;
    uint8_t exec_buffer[40];

    /* Validate unit number */
    if (unit > 3) {
        return status_$invalid_unit_number;
    }

    /* Get controller register address */
    DAT_00e7b020 = *(int32_t *)(&DAT_00e7afe0[ctlr * 8 + 4]);
    regs = (volatile flp_regs_t *)(uintptr_t)DAT_00e7b020;

    /*
     * On first initialization, wire the I/O buffer.
     * DAT_00e7b02c is the initialized flag (-1 = initialized).
     */
    if (DAT_00e7b02c >= 0) {
        /* Get physical address of I/O buffer */
        phys_addr = MMU__VTOP((uint32_t)(uintptr_t)FLP_IO_BUFFER, &vtop_status);

        /* Wire the buffer into physical memory */
        WP__WIRE(phys_addr);

        /*
         * Calculate physical address with offset within page.
         * phys_addr * 0x400 gives page frame, then add offset.
         */
        DAT_00e7aff0 = (phys_addr * 0x400) +
                       ((uint32_t)(uintptr_t)FLP_IO_BUFFER & 0x3FF);

        /* Mark as initialized */
        DAT_00e7b02c = -1;
    }

    /* Set control register to enable motor and select drive */
    regs->control = 3;  /* Motor on, drive select */

    /* Initialize unit state */
    DAT_00e7b026 = 0;   /* Clear control flag */
    DAT_00e7b00a = unit;  /* Set current unit */

    /* Execute recalibrate command */
    status = EXCS(DAT_00e7b008, DAT_00e3e21c, exec_buffer);

    /* Clear unit status */
    DAT_00e7af6c[unit * 2] = 0;
    DAT_00e7b018[unit] = 0;

    if (status == status_$ok && *params <= 0) {
        /*
         * Caller didn't provide geometry - return defaults.
         * Standard 3.5" HD floppy: 80 cylinders, 2 heads, 18 sectors
         * Total: 1232 (0x4D0) sectors
         */
        *flags = 0;
        *heads = 8;      /* Actually 2 heads * 4 = 8 for some reason */
        *sectors = 2;    /* Sectors per track encoding */
        *params = 0x4D0; /* Total sectors */

        /* Copy geometry data */
        geometry[0] = DAT_00e3e21e;
        geometry[1] = DAT_00e3e222;
        *(uint16_t *)&geometry[2] = DAT_00e3e226;
    }

    /* Set additional flag */
    *(uint16_t *)((uint8_t *)geometry + 6) = 1;

    return status;
}
