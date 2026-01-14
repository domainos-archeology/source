/*
 * SHAKE - Low-level floppy controller register handshaking
 *
 * This function performs the low-level handshake protocol with the
 * floppy controller. It either reads or writes bytes to/from the
 * controller's data register, depending on the data direction (DIO)
 * bit in the status register.
 *
 * The handshake loop:
 * 1. Wait for controller to be ready (RQM bit set, status >= 0x80)
 * 2. Check DIO bit for data direction
 * 3. Read or write data byte
 * 4. Repeat for specified count
 *
 * Timeout occurs after 2000 iterations of waiting for ready.
 */

#include "flp.h"

/* Status codes */
#define status_$disk_controller_timeout  0x00080003
#define status_$disk_controller_error    0x00080004

/* Current controller address */
extern int32_t DAT_00e7b020;

/*
 * SHAKE - Handshake data with controller
 *
 * @param data_buf   Data buffer (read into or write from)
 * @param count_ptr  Pointer to count of bytes to transfer
 * @param dir_ptr    Pointer to direction: 0=read, 1=write
 * @return Status code
 */
status_$t SHAKE(uint16_t *data_buf, int16_t *count_ptr, int16_t *dir_ptr)
{
    volatile flp_regs_t *regs;
    int16_t count;
    int16_t timeout;
    int16_t direction;

    regs = (volatile flp_regs_t *)(uintptr_t)DAT_00e7b020;
    direction = *dir_ptr;
    count = *count_ptr - 1;

    if (count < 0) {
        /* No bytes to transfer */
        return status_$ok;
    }

    do {
        /* Wait for controller ready with timeout */
        timeout = 2000;
        while ((int8_t)regs->status >= 0) {  /* Wait for RQM (bit 7) */
            timeout--;
            if (timeout <= 0) {
                return status_$disk_controller_timeout;
            }
        }

        /* Check data direction (DIO bit 6) */
        if ((regs->status & FLP_STATUS_DIO) != 0) {
            /* DIO=1: Controller has data to send (read direction) */
            if (direction != 0) {
                /* Expected write but controller wants to send */
                return status_$disk_controller_error;
            }
            /* Read byte from controller */
            *data_buf = (uint16_t)regs->data;
        } else {
            /* DIO=0: Controller expects data (write direction) */
            if (direction != 1) {
                /* Expected read but controller wants data */
                return status_$disk_controller_error;
            }
            /* Write byte to controller (low byte of word) */
            regs->data = (uint8_t)*data_buf;
        }

        data_buf++;
        count--;
    } while (count >= 0);

    return status_$ok;
}
