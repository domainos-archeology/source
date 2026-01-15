/*
 * EXCS - Execute floppy command and check status
 *
 * This function executes a floppy command by:
 * 1. Sending the command via SHAKE handshake
 * 2. Waiting for command completion via event counter
 * 3. Checking for DMA and parity errors
 * 4. Interpreting the status registers to determine outcome
 *
 * The function handles various error conditions including:
 * - Memory parity errors during writes
 * - DMA errors
 * - Drive not ready
 * - Write protection
 * - Data CRC errors
 * - Equipment check failures
 * - Format errors (bad disk format, wrong side count)
 * - DMA overruns
 */

#include "flp/flp_internal.h"

/* Status codes for floppy operations */
#define status_$memory_parity_error_during_disk_write  0x00080025
#define status_$dma_not_at_end_of_range                0x0008001d
#define status_$disk_not_ready                         0x00080001
#define status_$disk_equipment_check                   0x00080005
#define status_$floppy_is_not_2_sided                  0x00080006
#define status_$disk_write_protected                   0x00080007
#define status_$bad_disk_format                        0x00080008
#define status_$disk_data_check                        0x00080009
#define status_$DMA_overrun                            0x0008000a
#define status_$unknown_status_returned_by_hardware    0x00080019

/* Retry marker - indicates operation should be retried */
#define FLP_RETRY_NEEDED  0x0008ffff

/* FLP data area base address */
#define FLP_DATA_BASE     0x00e7aef4

/* Status register bit definitions */
#define FLP_ST0_ABNORMAL_TERM    0x08  /* Abnormal termination */
#define FLP_ST0_EQUIP_CHECK      0x10  /* Equipment check */
#define FLP_ST0_NOT_READY        0xc0  /* Drive not ready */
#define FLP_ST0_STATUS_MASK      0xd8  /* Relevant status bits */

#define FLP_ST1_END_OF_CYL       0x80  /* End of cylinder */
#define FLP_ST1_DATA_ERROR       0x20  /* Data error (CRC) */
#define FLP_ST1_OVERRUN          0x10  /* Overrun */
#define FLP_ST1_NO_DATA          0x04  /* No data */
#define FLP_ST1_NOT_WRITABLE     0x02  /* Not writable */
#define FLP_ST1_MISSING_AM       0x01  /* Missing address mark */

#define FLP_ST2_CONTROL_MARK     0x40  /* Control mark */
#define FLP_ST2_DATA_ERROR       0x20  /* Data error in data field */
#define FLP_ST2_WRONG_CYL        0x10  /* Wrong cylinder */
#define FLP_ST2_BAD_CYL          0x02  /* Bad cylinder */
#define FLP_ST2_MISSING_DAM      0x01  /* Missing data address mark */

/*
 * EXCS - Execute command and check status
 *
 * @param cmd_buf    Command buffer to send
 * @param cmd_size   Pointer to command size / direction data
 * @param req        Request block (contains flags at offset 0x29)
 * @return Status code (0 = success, 0x8ffff = retry, else error)
 */
status_$t EXCS(uint16_t *cmd_buf, void *cmd_size, void *req)
{
    status_$t status;
    status_$t result;
    int16_t wait_result;
    int16_t parity_result;
    uint16_t local_regs[3];  /* Local status register buffer */
    uint32_t *ec_value_ptr;
    volatile flp_regs_t *regs;
    uint32_t ec_list[6];     /* Event counter wait list */

    /* Get event counter value + 1 for wait comparison */
    ec_value_ptr = (uint32_t *)((uintptr_t)&FLP_$EC + sizeof(uint32_t));
    uint32_t wait_value = *ec_value_ptr + 1;

    /* Send command to controller via SHAKE */
    status = SHAKE(cmd_buf, (int16_t *)cmd_size, &DAT_00e3e110);
    if (status != status_$ok) {
        return status;
    }

    /*
     * Wait for command completion.
     * EC_$WAIT takes an event counter list and a value to wait for.
     * The list format is complex - contains pointers to EC structures.
     */
    /* Build event counter wait list */
    ec_list[0] = (uint32_t)(uintptr_t)&FLP_$EC;
    ec_list[1] = DAT_00e2b0d4 + 8;  /* Secondary EC address */

    wait_result = EC_$WAIT((void *)ec_list, &wait_value);

    /* Check for DMA and parity errors (unless command was interrupt sense) */
    if ((cmd_buf[0] & 7) != 7) {  /* Not sense interrupt command */
        regs = (volatile flp_regs_t *)(uintptr_t)DAT_00e7b020;

        /* Check for parity errors on write operations */
        if ((regs->control & 2) != 0) {
            parity_result = PARITY_$CHK_IO(1, DAT_00e7b01c);
            if ((int8_t)(-(parity_result != 0)) < 0) {
                return status_$memory_parity_error_during_disk_write;
            }
        }

        /* Check for DMA errors */
        status = check_dma_error(3);
        if (status != status_$ok && status != status_$dma_not_at_end_of_range) {
            goto check_retry;
        }
    }

    /* If wait timed out, set status to indicate seek error */
    if (wait_result != 0) {
        FLP_$SREGS = 0x10;  /* Indicate abnormal termination */
    }

    /* Check if status indicates any error condition */
    if ((FLP_$SREGS & FLP_ST0_STATUS_MASK) == 0) {
        /* No errors - command completed successfully */
        return status_$ok;
    }

    /*
     * Error detected - need to send sense interrupt status command
     * to get detailed error information.
     */
    DAT_00e7b006 = cmd_buf[1];  /* Copy unit/head info */

    /* Send sense drive status command */
    status = SHAKE((uint16_t *)&DAT_00e7b004, &DAT_00e3e21c, &DAT_00e3e110);
    if (status != status_$ok) {
        result = status;
        goto check_retry;
    }

    /* Read result bytes */
    status = SHAKE(local_regs, &DAT_00e3e110, &DAT_00e3e10e);
    if (status != status_$ok) {
        result = status;
        goto check_retry;
    }

    /*
     * Interpret status registers to determine error type.
     * FLP_$SREGS[high byte] is ST0, [low byte] is ST1
     * DAT_00e7af66[high byte] is ST2
     */

    /* Check for equipment check (ST0 bit 4) */
    if ((FLP_$SREGS & FLP_ST0_EQUIP_CHECK) != 0) {
        result = status_$disk_equipment_check;
        goto check_retry;
    }

    /* Check for abnormal termination (ST0 bit 3) */
    if ((FLP_$SREGS & FLP_ST0_ABNORMAL_TERM) != 0) {
        DAT_00e7b026 = 0;  /* Clear retry flag */

        /* Check result register for specific conditions */
        if ((local_regs[0] & 0x08) == 0 && (local_regs[0] & 0x20) != 0) {
            /* Head 1 access on single-sided disk */
            if (cmd_buf[1] >= 4) {  /* Head number > 0 */
                result = status_$floppy_is_not_2_sided;
                goto check_retry;
            }
        }
        result = status_$disk_not_ready;
        goto check_retry;
    }

    /* Check if drive is not ready (ST0 bits 7:6) */
    if ((~FLP_$SREGS & FLP_ST0_NOT_READY) == 0) {
        result = status_$disk_not_ready;
        goto clear_flag_and_return;
    }

    /* Check for write protected disk (ST1 bit 1) */
    if ((DAT_00e7af66 & FLP_ST1_NOT_WRITABLE) != 0) {
        result = status_$disk_write_protected;
        goto clear_flag_and_return;
    }

    /* Check for bad format errors (ST1 bits 7,2,0) */
    if ((DAT_00e7af66 & (FLP_ST1_END_OF_CYL | FLP_ST1_NO_DATA | FLP_ST1_MISSING_AM)) != 0) {
        result = status_$bad_disk_format;

        /* Check if we should retry with recalibrate */
        if ((DAT_00e7af69 & FLP_ST2_WRONG_CYL) != 0 && DAT_00e7b026 != 0) {
            DAT_00e7b026 = 1;  /* Set recalibrate flag */

            /* Extract unit number and recalibrate */
            uint16_t unit = cmd_buf[1] & 3;
            DAT_00e7b00a = unit;

            status = EXCS(&DAT_00e7b008, &DAT_00e3e21c, req);

            /* Clear unit status */
            DAT_00e7af6c[unit * 2] = 0;

            if (status != status_$ok) {
                result = status;
            }
            goto check_retry;
        }
        goto check_retry;
    }

    /* Check for data error (ST1 bit 5) */
    if ((DAT_00e7af66 & FLP_ST1_DATA_ERROR) != 0) {
        result = status_$disk_data_check;

        /* Check if caller has indicated to ignore data check errors */
        if ((*(uint8_t *)((uint8_t *)req + 0x29) & 2) != 0) {
            return status_$disk_data_check;
        }
        goto check_retry;
    }

    /* Check for DMA overrun (ST1 bit 4) */
    if ((DAT_00e7af66 & FLP_ST1_OVERRUN) != 0) {
        if (DAT_00e7b024 > 0) {
            /* Retry available */
            DAT_00e7b024--;
            return FLP_RETRY_NEEDED;
        }
        result = status_$DMA_overrun;
        goto check_retry_and_exit;
    }

    /* Unknown error condition */
    result = status_$unknown_status_returned_by_hardware;
    goto check_retry;

clear_flag_and_return:
    DAT_00e7b026 = 0;  /* Clear control flag */
    /* Fall through to check_retry */

check_retry:
    if (DAT_00e7b026 != 0) {
        DAT_00e7b026--;
        return FLP_RETRY_NEEDED;
    }

check_retry_and_exit:
    return result;
}
