/*
 * FLP_$INT - Floppy disk interrupt handler
 *
 * This function handles interrupts from the floppy disk controller.
 * It reads status and result bytes from the controller's data register
 * and stores them in the saved registers array.
 *
 * The floppy controller generates interrupts when:
 * - A command completes
 * - Seek completes
 * - Disk change detected
 *
 * The controller status register indicates data direction (DIO bit)
 * which determines whether we should read result bytes.
 */

#include "flp.h"

/* Controller table - 8 bytes per controller entry */
extern uint8_t DAT_00e7afe0[];

/* Saved status registers from result phase */
extern uint16_t FLP_$SREGS_ARRAY[];  /* Array at FLP_$JUMP_TABLE + 0x1c */

/* Disk change flags - one byte per unit */
extern uint8_t DAT_00e7b018[FLP_MAX_UNITS];

/* Current controller address */
extern int32_t DAT_00e7b020;

/* Event counter for floppy operations */
extern void *FLP_$EC;

/*
 * FLP_$INT - Handle floppy interrupt
 *
 * @param int_info  Interrupt information structure
 *                  (offset 0x06 contains controller index)
 * @return 0xFF (interrupt handled)
 */
uint16_t FLP_$INT(void *int_info)
{
    int16_t ctlr_index;
    volatile flp_regs_t *regs;
    int16_t result_count;
    int8_t done;
    uint16_t *result_ptr;
    uint16_t status_byte;

    /* Get controller index from interrupt info */
    ctlr_index = *(int16_t *)((uint8_t *)int_info + 6);

    /* Look up controller address from controller table */
    DAT_00e7b020 = *(int32_t *)(&DAT_00e7afe0[ctlr_index * 8 + 4]);
    regs = (volatile flp_regs_t *)(uintptr_t)DAT_00e7b020;

    result_count = 0;
    done = 0;
    result_ptr = (uint16_t *)((uint8_t *)&FLP_$JUMP_TABLE + 0x38);  /* SREGS array */

    do {
        /* Wait for controller to be ready (not busy) */
        while ((int8_t)regs->status >= 0) {
            /* Busy-wait: status bit 7 (busy) is set */
        }

        /* Check DIO bit to determine data direction */
        if ((regs->status & FLP_STATUS_DIO) == 0) {
            /*
             * DIO=0: Controller to host direction (result phase)
             * but no data available - this is unusual
             */
            if (result_count == 0) {
                /* First byte - write to data register to request status */
                regs->data = 8;  /* Sense interrupt status command */
            } else {
                /* We've read at least one byte, we're done */
                done = -1;
            }
        } else {
            /*
             * DIO=1: Controller has data to send (result bytes)
             * Read the data byte
             */
            status_byte = (uint16_t)regs->data;

            if (result_count < 3) {
                /* Store up to 3 result bytes */
                *result_ptr = status_byte;
                result_count++;
                result_ptr++;
            }
        }
    } while (done >= 0);

    /*
     * Check if this was a disk change interrupt.
     * If status register bits [2:0] == 6, set disk change flag
     * for the unit indicated by bits [1:0].
     */
    if ((FLP_$SREGS & 7) == 6) {
        int16_t unit = FLP_$SREGS & 3;
        DAT_00e7b018[unit] = 0xFF;
    }

    /* Signal completion via event counter */
    EC_$ADVANCE_WITHOUT_DISPATCH(&FLP_$EC);

    return 0xFF;  /* Interrupt handled */
}
