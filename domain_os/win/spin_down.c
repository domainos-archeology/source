/*
 * WIN_$SPIN_DOWN - Spin down Winchester disk
 *
 * Sends the ANSI spin control command to spin down the disk.
 *
 * @param unit_ptr  Pointer to unit number
 * @return          0x14 on success, high word of status on error
 */

#include "win.h"

uint32_t WIN_$SPIN_DOWN(uint16_t *unit_ptr)
{
    status_$t status;
    char temp_buf[6];

    /* Send spin down command (0x55) */
    status = WIN_$ANSI_COMMAND(*unit_ptr, ANSI_CMD_SPIN_CONTROL, NULL, temp_buf);

    if (status == status_$ok) {
        return 0x14;  /* Return spin down delay */
    } else {
        return status & 0xffff0000;  /* Return high word of status */
    }
}
