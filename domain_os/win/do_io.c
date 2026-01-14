/*
 * WIN_$DO_IO - Winchester Disk I/O Handler
 *
 * Main I/O entry point for Winchester disk operations.
 * Handles read, write, and format operations with retry logic
 * for transient errors.
 *
 * Operation types (from request byte at +0x1f, low nibble):
 *   0x02 = Read
 *   0x03 = Format (handled specially)
 *
 * @param dev_entry  Device entry pointer
 * @param req        I/O request chain (linked list)
 * @param param_3    Additional parameter
 * @param result     Output: result byte
 */

#include "win.h"

/* Maximum retry counts */
#define MAX_DMA_RETRIES      500
#define MAX_OTHER_RETRIES    24  /* 0x17 + 1 */

void WIN_$DO_IO(void *dev_entry, int32_t *req, void *param_3, uint8_t *result)
{
    uint8_t *win_data = WIN_DATA_BASE;
    int16_t resource_id;
    uint8_t op_type;
    status_$t status;
    int16_t dma_retries;
    int16_t other_retries;
    uint16_t *cylinder_ptr;
    void *wait_val;
    int16_t wait_result;
    int32_t *local_req;

    /* Clear result */
    *result = 0;

    /* Get resource lock ID */
    resource_id = *(int16_t *)(win_data + WIN_DEV_TYPE_OFFSET);

    /* Get operation type from request */
    op_type = *(uint8_t *)((uint8_t *)req + 0x1f) & 0x0f;

    /* Handle format operation specially */
    if (op_type == 3) {
        ML_$LOCK(resource_id);
        FUN_00e196aa(dev_entry);
        ML_$UNLOCK(resource_id);
        return;
    }

    /* For write operations with linked requests, sort by cylinder */
    if (op_type == 2 && *req != 0) {
        local_req = req;
        DISK_$SORT(dev_entry, (void **)&local_req);
        req = local_req;
    }

    /* Acquire resource lock */
    ML_$LOCK(resource_id);

    /* Save current request info */
    *(void **)(win_data + WIN_DEV_INFO_OFFSET) = dev_entry;
    *(int32_t **)(win_data + WIN_REQ_PTR_OFFSET) = req;

    /* Initialize retry counters */
    dma_retries = 0;
    other_retries = MAX_OTHER_RETRIES - 1;

    /* Get cylinder pointer from device entry */
    cylinder_ptr = (uint16_t *)((uint8_t *)dev_entry + 0x1c);

    /* Main I/O loop with retries */
retry_loop:
    /* Get wait value for event counter */
    wait_val = (void *)(*(uint32_t *)(win_data + WIN_EC_ARRAY_OFFSET) + 1);

    /* Seek to cylinder */
    status = SEEK(0, *cylinder_ptr, *(void **)(win_data + WIN_REQ_PTR_OFFSET), 0);

    if (status == status_$ok) {
        /* Perform read or write */
        status = read_or_write_disk_record(0);

    wait_for_completion:
        if ((int32_t)status < 1) {
            /* Wait for I/O completion */
            wait_result = EC_$WAIT((void *)(win_data + WIN_EC_ARRAY_OFFSET), wait_val);

            if (wait_result != 0) {
                /* Timeout - clear flag and set error */
                win_data[WIN_FLAG_OFFSET] = 0;
                *(status_$t *)(win_data + WIN_STATUS_OFFSET) = status_$disk_controller_timeout;
            }

            status = *(status_$t *)(win_data + WIN_STATUS_OFFSET);
            if (status == status_$ok) {
                goto done;
            }
        }

        /* Handle specific errors */
        if (status == status_$DMA_overrun) {
            dma_retries++;
            if (dma_retries < MAX_DMA_RETRIES) {
                goto retry_loop;
            }
            goto error_exit;
        }

        if (status == status_$memory_parity_error_during_disk_write) {
            /* Check if parity error is real */
            int32_t *cur_req = *(int32_t **)(win_data + WIN_REQ_PTR_OFFSET);
            int16_t parity_result = PARITY_$CHK_IO(
                (uint32_t)cur_req[4] >> 10,
                cur_req[5]);
            if (-parity_result < 0) {
                goto error_exit;
            }
            goto retry;
        }

        if (status == status_$disk_data_check) {
            int32_t *cur_req = *(int32_t **)(win_data + WIN_REQ_PTR_OFFSET);
            if (*(int8_t *)((uint8_t *)cur_req + 0x1f) < 0) {
                goto error_exit;
            }
            /* Fall through to retry */
        }

        if (status == status_$disk_not_ready ||
            status == status_$unknown_error_status_from_drive ||
            status == status_$disk_equipment_check) {
            FUN_00e194b4(0, *cylinder_ptr);
        }

    retry:
        other_retries--;
        if (other_retries >= 0) {
            goto retry_loop;
        }
    } else if (status == status_$disk_seek_error) {
        /* Recalibrate and retry seek */
        status_$t recal_status = FUN_00e194b4(0, *cylinder_ptr);
        if (recal_status != status_$ok) {
            status = recal_status;
        }
        goto retry;
    } else {
        goto wait_for_completion;
    }

error_exit:
    /* Mark all remaining requests as failed */
    if (status != status_$ok) {
        int32_t *cur_req = *(int32_t **)(win_data + WIN_REQ_PTR_OFFSET);
        uint8_t vol_idx = *(uint8_t *)((uint8_t *)cur_req + 0x1e);

        /* Clear something in process table */
        /* (&DAT_00e7a55c)[vol_idx * 0x1c] = 0; */

        /* Set status in current request */
        cur_req[3] = status;

        /* Mark remaining requests as failed */
        while ((cur_req = (int32_t *)*cur_req) != NULL) {
            cur_req[3] = -1;
        }
    }

done:
    ML_$UNLOCK(resource_id);
}
