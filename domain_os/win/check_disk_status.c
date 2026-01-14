/*
 * WIN_$CHECK_DISK_STATUS - Check Winchester disk status
 *
 * Reads and interprets the disk status register, handling various
 * error conditions and updating statistics counters.
 *
 * @param unit  Unit number
 * @return      Status code indicating disk state
 */

#include "win.h"

status_$t WIN_$CHECK_DISK_STATUS(uint16_t unit)
{
    uint8_t *win_data = WIN_DATA_BASE;
    uint8_t *unit_data;
    uint32_t unit_offset;
    uint16_t disk_status;
    status_$t primary_status = status_$ok;
    status_$t secondary_status;
    uint16_t extended_status;
    uint16_t *stats;
    uint16_t clear_cmd = 0;
    char temp_buf[4];
    uint16_t status_result[9];

    /* Clear extended status byte */
    win_data[WIN_EXT_STATUS_OFFSET] = 0;

    /* Get unit's data buffer */
    unit_offset = (uint32_t)unit * WIN_UNIT_ENTRY_SIZE;
    unit_data = *(uint8_t **)(win_data + unit_offset + 4);

    /* Read disk status word */
    disk_status = *(uint16_t *)(unit_data + 6);
    *(uint16_t *)(win_data + WIN_DISK_STATUS_OFFSET) = disk_status;

    /* Check if status complete (bit 11 set) */
    if ((disk_status & 0x800) != 0) {
        /* Clear command pending flag */
        unit_data[0x0e] = 0;

        /* Check for error bits (mask 0xfa) */
        if ((disk_status & 0xfa) != 0) {
            /* Bit 3: Controller error - crash system */
            if ((disk_status & 0x08) != 0) {
                CRASH_SYSTEM(&Disk_controller_err);
            }
            /* Bit 4: Equipment check */
            else if ((disk_status & 0x10) != 0) {
                stats = (uint16_t *)(win_data + 0x4e);
                (*stats)++;
                primary_status = status_$disk_equipment_check;
            }
            /* Bit 1: Memory parity error during write */
            else if ((disk_status & 0x02) != 0) {
                primary_status = status_$memory_parity_error_during_disk_write;
            }
            /* Bit 6: DMA overrun */
            else if ((disk_status & 0x40) != 0) {
                stats = (uint16_t *)(win_data + 0x54);
                (*stats)++;
                primary_status = status_$DMA_overrun;
            }
            /* Bit 5: Data check */
            else if ((disk_status & 0x20) != 0) {
                stats = (uint16_t *)(win_data + 0x52);
                (*stats)++;
                primary_status = status_$disk_data_check;
            }
            /* Other error - disk not ready */
            else {
                stats = (uint16_t *)(win_data + 0x48);
                (*stats)++;
                primary_status = status_$disk_not_ready;
            }
        }
    }

    /* Check for extended status */
    if ((disk_status & 0x2000) != 0) {
        /* Get extended status via ANSI command */
        secondary_status = WIN_$ANSI_COMMAND(unit, ANSI_CMD_REPORT_GENERAL_STATUS,
                                             NULL, (char *)&extended_status);
        if (secondary_status != status_$ok) {
            return secondary_status;
        }
        clear_cmd = 2;
    } else if ((disk_status & 0x1000) != 0) {
        /* Extended status in command byte */
        extended_status = ((uint16_t)unit_data[0]) << 8;
    } else {
        /* No extended status */
        return (primary_status != status_$ok) ? primary_status : status_$ok;
    }

    /* Save extended status */
    *(uint16_t *)(win_data + WIN_EXT_STATUS_OFFSET) = extended_status;

    /* Process extended status */
    FUN_00e19186(unit, (char)(extended_status >> 8), status_result);

    /* Issue clear command if needed */
    if (status_result[0] != 0 || clear_cmd != 0) {
        if (status_result[0] == 0) {
            status_result[0] = clear_cmd;
        }
        WIN_$ANSI_COMMAND(unit, status_result[0], NULL, temp_buf);
    }

    secondary_status = status_$ok;

    /* Check extended status bits */
    if ((extended_status & 0x200) != 0) {
        /* Fault condition - clear it */
        WIN_$ANSI_COMMAND(unit, ANSI_CMD_CLEAR_FAULT, NULL, temp_buf);
        stats = (uint16_t *)(win_data + 0x4e);
        (*stats)++;
        secondary_status = status_$disk_equipment_check;
    } else if ((extended_status & 0x4100) != 0) {
        /* Not ready conditions */
        stats = (uint16_t *)(win_data + 0x48);
        (*stats)++;
        secondary_status = status_$disk_not_ready;
    } else if ((extended_status & 0x4c00) != 0) {
        /* Logic error */
        CRASH_SYSTEM(&Disk_driver_logic_err);
    }

    /* Return primary status if set, otherwise secondary */
    return (primary_status != status_$ok) ? primary_status : secondary_status;
}
