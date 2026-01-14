/*
 * WIN_$ANSI_COMMAND - Send ANSI command to Winchester drive
 *
 * Sends an ANSI standard command to the Winchester disk controller.
 * Some commands take input parameters, others return output parameters.
 *
 * @param unit            Unit number
 * @param ansi_cmd        ANSI command code
 * @param ansi_in_param   Input parameter (used for commands >= 0x40)
 * @param ansi_out_param  Output parameter (used for commands < 0x40)
 * @return                Status code
 */

#include "win.h"

/* Uses declaration from win.h */

status_$t WIN_$ANSI_COMMAND(uint16_t unit, uint16_t ansi_cmd,
                            char *ansi_in_param, char *ansi_out_param)
{
    uint8_t *win_data = WIN_DATA_BASE;
    uint8_t *unit_data;
    uint32_t unit_offset;
    status_$t status;
    int8_t has_input;

    /* Get unit's command buffer */
    unit_offset = (uint32_t)unit * WIN_UNIT_ENTRY_SIZE;
    unit_data = *(uint8_t **)(win_data + unit_offset + 4);

    /* Check if command takes input parameter (commands >= 0x40) */
    has_input = -(ansi_cmd > 0x3f);

    /* Set command byte */
    unit_data[0] = (char)ansi_cmd;

    /* Copy input parameter if needed */
    if (has_input < 0) {
        unit_data[2] = *ansi_in_param;
    }

    /* Set command type in status byte */
    unit_data[0x0e] = 5;

    /* Execute command */
    status = FUN_00e190bc(unit);

    /* Copy output parameter if command returns data */
    if (has_input >= 0) {
        *ansi_out_param = unit_data[2];
    }

    return status;
}
