/*
 * FLP_$CINIT - Floppy controller initialization
 *
 * This function initializes a floppy disk controller. It:
 * 1. Probes for the controller hardware
 * 2. Initializes the controller registers
 * 3. Sets up the event counter for interrupt synchronization
 * 4. Registers the controller with the disk subsystem
 *
 * The initialization waits for the controller to become ready
 * by polling the status register until the busy flag clears.
 */

#include "flp.h"

/* External function to probe for hardware */
extern int8_t FUN_00e29138(void *signature, void *hw_addr, void *buffer);

/* Controller table - 8 bytes per controller */
extern uint8_t DAT_00e7afdc[];  /* Controller info pointer */
extern uint8_t DAT_00e7afe0[];  /* Controller register address */

/* Current controller address */
extern int32_t DAT_00e7b020;

/* Event counter */
extern void *FLP__EC;

/* Jump table */
extern void *FLP__JUMP_TABLE;

/* Status registers */
extern uint16_t FLP__SREGS;

/* Registration data areas */
extern uint8_t DAT_00e7affc[];
extern uint8_t DAT_00e7b02a[];

/* Hardware signature data */
extern uint8_t DAT_00e3e10e[];
extern uint8_t DAT_00e3e110[];
extern uint8_t DAT_00e3ddc2[];

/*
 * FLP_$CINIT - Initialize floppy controller
 *
 * @param ctlr_info  Controller information structure
 *                   +0x06: Controller number
 *                   +0x34: Hardware address
 *                   +0x3c: Configuration data
 * @return Status code
 */
status_$t FLP_$CINIT(void *ctlr_info)
{
    uint16_t ctlr_num;
    int32_t hw_addr;
    volatile flp_regs_t *regs;
    uint16_t retry;
    status_$t status;
    int8_t found;
    uint8_t probe_buffer[10];
    uint16_t local_regs[2];
    void *jump_table_ptr[2];

    /* Probe for controller hardware */
    found = FUN_00e29138(DAT_00e3e10e,
                         (uint8_t *)ctlr_info + 0x34,
                         probe_buffer);

    if (found >= 0) {
        /* Controller not found */
        return status_$io_controller_not_in_system;
    }

    /* Get controller number and hardware address */
    ctlr_num = *(uint16_t *)((uint8_t *)ctlr_info + 6);
    hw_addr = *(int32_t *)((uint8_t *)ctlr_info + 0x34);
    DAT_00e7b020 = hw_addr;

    /* Store in controller table */
    *(void **)(&DAT_00e7afdc[ctlr_num * 8]) = ctlr_info;
    *(int32_t *)(&DAT_00e7afe0[ctlr_num * 8]) = hw_addr;

    /* Initialize event counter */
    EC__INIT(&FLP__EC);

    regs = (volatile flp_regs_t *)(uintptr_t)hw_addr;
    status = status_$ok;

    /*
     * Wait for controller to become ready.
     * Poll status register until command status bits are clear.
     * Retry up to 200 times (0xC8).
     */
    for (retry = 0; retry <= 200; retry++) {
        /* Check if status register command bits are clear */
        if ((regs->status & FLP_STATUS_CMD_MASK) == 0) {
            /* Controller ready - continue with initialization */
            goto controller_ready;
        }

        /* Controller busy - try to clear it */
        if ((regs->status & FLP_STATUS_DIO) == 0) {
            /* DIO=0: Send dummy command to reset */
            local_regs[0] = 8;  /* Sense interrupt status */
            SHAKE(local_regs, DAT_00e3e110, DAT_00e3e110);
        } else {
            /* DIO=1: Read result bytes to clear */
            SHAKE(&FLP__SREGS, DAT_00e3ddc2, DAT_00e3e10e);
        }
    }

    /* Timeout waiting for controller */
    return status_$disk_controller_error;

controller_ready:
    /* Send specify command to configure controller parameters */
    status = SHAKE(DAT_00e7affc, DAT_00e3ddc2, DAT_00e3e110);
    if (status != status_$ok) {
        return status;
    }

    /* Register with disk subsystem */
    jump_table_ptr[0] = &FLP__JUMP_TABLE;
    local_regs[0] = ctlr_num;
    DISK__REGISTER(DAT_00e3e110,
                   local_regs,
                   DAT_00e7b02a,
                   (uint8_t *)ctlr_info + 0x3c,
                   jump_table_ptr);

    return status;
}
