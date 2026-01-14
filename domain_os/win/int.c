/*
 * WIN_$INT - Winchester Interrupt Handler
 *
 * Handles interrupts from the Winchester disk controller.
 * Processes completion of I/O operations and starts next
 * request in queue if available.
 *
 * @param param  Interrupt parameter (contains unit number at +6)
 * @return       0xff (always)
 */

#include "win.h"

uint32_t WIN_$INT(void *param)
{
    uint8_t *win_data = WIN_DATA_BASE;
    uint16_t unit;
    uint32_t unit_offset;
    uint8_t *unit_data;
    int32_t *cur_req;
    status_$t status;
    int8_t done_flag;

    /* Get unit number from parameter */
    unit = *(uint16_t *)((uint8_t *)param + 6);

    /* Get unit data pointer */
    unit_offset = (uint32_t)unit * WIN_UNIT_ENTRY_SIZE;
    unit_data = *(uint8_t **)(win_data + unit_offset + 4);

    /* Clear command pending flag */
    unit_data[0x0c] = 0;

    /* Get current request */
    cur_req = *(int32_t **)(win_data + WIN_REQ_PTR_OFFSET);
    if (cur_req == NULL) {
        goto signal_done;
    }

    done_flag = 0;

    /* Raise interrupt priority and check status */
    /* Note: Original sets SR to 0x2500 here */

    *(status_$t *)(win_data + WIN_STATUS_OFFSET) = WIN_$CHECK_DISK_STATUS(unit);

    /* Check if we're in the middle of a multi-sector operation */
    if (win_data[WIN_FLAG_OFFSET] < 0) {
        win_data[WIN_FLAG_OFFSET] = 0;

        if (*(status_$t *)(win_data + WIN_STATUS_OFFSET) == status_$ok) {
            /* Save cylinder from request */
            *(uint16_t *)(win_data + WIN_CUR_CYL_OFFSET) =
                *(uint16_t *)(cur_req + 1);

            /* Continue with read/write */
            goto continue_io;
        }
    } else {
        /* Check DMA completion */
        status = check_dma_error(3);
        if (*(status_$t *)(win_data + WIN_STATUS_OFFSET) == status_$ok) {
            *(status_$t *)(win_data + WIN_STATUS_OFFSET) = status;
        }

        if (*(status_$t *)(win_data + WIN_STATUS_OFFSET) == status_$ok) {
            /* Move to next request in chain */
            cur_req = (int32_t *)*cur_req;
            *(int32_t **)(win_data + WIN_REQ_PTR_OFFSET) = cur_req;

            if (cur_req == NULL) {
                done_flag = -1;
            } else {
                /* Seek to next cylinder and continue I/O */
                uint8_t *dev_info = *(uint8_t **)(win_data + WIN_DEV_INFO_OFFSET);
                status = SEEK(unit, *(uint16_t *)(dev_info + 0x1c),
                             cur_req, done_flag & 0xff);

                if (status == status_$ok) {
                continue_io:
                    status = read_or_write_disk_record(unit);
                    if (status > 0) {
                        *(status_$t *)(win_data + WIN_STATUS_OFFSET) = status;
                    }
                }
            }
        }
    }

    /* Check if we need to signal completion */
    if (*(status_$t *)(win_data + WIN_STATUS_OFFSET) != status_$ok) {
        done_flag = -1;
    }

    if (done_flag >= 0) {
        return 0xff;
    }

signal_done:
    /* Signal event counter for completion */
    EC_$ADVANCE_WITHOUT_DISPATCH((void *)(win_data + WIN_EC_ARRAY_OFFSET + unit_offset));

    return 0xff;
}
