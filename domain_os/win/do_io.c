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

#include "win/win_internal.h"
#include "time/time.h"

/* Maximum retry counts */
#define MAX_DMA_RETRIES      500
#define MAX_OTHER_RETRIES    24  /* 0x17 + 1 */

void WIN_$DO_IO(void *dev_entry, win_$request_t *req, void *param_3, uint8_t *result)
{
    uint8_t *win_data = WIN_DATA_BASE;
    int16_t resource_id;
    uint8_t op_type;
    status_$t status;
    int16_t dma_retries;
    int16_t other_retries;
    uint16_t *cylinder_ptr;
    ec_$eventcount_t *win_ec;
    int32_t wait_val;
    int16_t wait_result;
    win_$request_t *local_req;

    /* Clear result */
    *result = 0;

    /* Get resource lock ID */
    resource_id = *(int16_t *)(win_data + WIN_DEV_TYPE_OFFSET);

    /* Get operation type from request */
    op_type = (uint8_t)req->flags & 0x0F;

    /* Handle format operation specially */
    if (op_type == 3) {
        ML_$LOCK(resource_id);
        WIN_$FORMAT_TRACK(dev_entry, req);
        ML_$UNLOCK(resource_id);
        return;
    }

    /* For write operations with linked requests, sort by cylinder */
    if (op_type == 2 && req->next != 0) {
        local_req = req;
        DISK_$SORT(dev_entry, (void **)&local_req);
        req = local_req;
    }

    /* Acquire resource lock */
    ML_$LOCK(resource_id);

    /* Save current request info */
    *(void **)(win_data + WIN_DEV_INFO_OFFSET) = dev_entry;
    *(win_$request_t **)(win_data + WIN_REQ_PTR_OFFSET) = req;

    /* Initialize retry counters */
    dma_retries = 0;
    other_retries = MAX_OTHER_RETRIES - 1;

    /* Get cylinder pointer from device entry */
    cylinder_ptr = (uint16_t *)((uint8_t *)dev_entry + 0x1c);

    /* Unit 0's completion eventcount (00e19888 pea (0x30,A5)) */
    win_ec = (ec_$eventcount_t *)(win_data + WIN_EC_ARRAY_OFFSET);

    /* Main I/O loop with retries */
retry_loop:
    /*
     * Get wait value for event counter
     * 00e1981a  move.l (0x30,A5),D0
     * 00e1981e  addq.l #0x1,D0
     * 00e19820  move.l D0,(-0x10,A6)
     */
    wait_val = win_ec->value + 1;

    /* Seek to cylinder */
    status = SEEK(0, *cylinder_ptr, *(void **)(win_data + WIN_REQ_PTR_OFFSET), 0);

    if (status == status_$ok) {
        /* Perform read or write */
        status = read_or_write_disk_record(0);

    wait_for_completion:
        if ((int32_t)status < 1) {
            /*
             * Wait for I/O completion, with an 8-tick clock timeout.
             * The original builds the two by-value arrays on the stack; the
             * three pointers are pushed last so they land at the lower
             * addresses (00e19872 - 00e1988c):
             *   00e19872  clr.l D2
             *   00e19874  move.l D2,-(SP)           vals[2] = 0
             *   00e19876  move.l (0x00e2b0d4).l,D0
             *   00e1987c  addq.l #0x8,D0
             *   00e1987e  move.l D0,-(SP)           vals[1] = TIME_$CLOCKH + 8
             *   00e19880  move.l (-0x10,A6),-(SP)   vals[0] = win_ec->value + 1
             *   00e19884  pea (A4)   A4 = 0         ecs[2] = NULL
             *   00e19886  pea (A3)   A3 = 0xe2b0d4  ecs[1] = &TIME_$CLOCKH
             *   00e19888  pea (0x30,A5)             ecs[0] = win_ec
             *   00e1988c  jsr EC_$WAIT
             *   00e19896  tst.w D0w                 ; 0-based index
             * D2 (status) is cleared by the clr.l at 00e19872 that also
             * supplies vals[2].
             */
            status = status_$ok;
            wait_result = EC_$WAIT(
                (ec_$wait_ecs_t){{ win_ec,
                                   (ec_$eventcount_t *)&TIME_$CLOCKH,
                                   NULL }},
                (ec_$wait_vals_t){{ wait_val,
                                    (int32_t)(TIME_$CLOCKH + 8),
                                    0 }});

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
            win_$request_t *cur_req =
                *(win_$request_t **)(win_data + WIN_REQ_PTR_OFFSET);
            int16_t parity_result = (int16_t)PARITY_$CHK_IO(
                cur_req->pa >> 10,
                cur_req->length);
            if (-parity_result < 0) {
                goto error_exit;
            }
            goto retry;
        }

        if (status == status_$disk_data_check) {
            win_$request_t *cur_req =
                *(win_$request_t **)(win_data + WIN_REQ_PTR_OFFSET);
            if (cur_req->flags < 0) {
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
        win_$request_t *cur_req =
            *(win_$request_t **)(win_data + WIN_REQ_PTR_OFFSET);

        /*
         * 0x00E19934-0x00E1994A: clear the requesting process's pending-I/O
         * byte.  Same five instructions as WIN_$FORMAT_TRACK
         * 0x00E19750-0x00E19764.
         */
        WIN_IO_PENDING(cur_req->proc_id) = 0;

        /* 0x00E1994E `move.l D2,(0xc,A0)`. */
        cur_req->status = status;

        /* 0x00E19956-0x00E19962: every queued request behind it fails. */
        while ((cur_req = (win_$request_t *)ARCH_VA_TO_PTR(cur_req->next))
                   != NULL) {
            cur_req->status = (status_$t)-1;
        }
    }

done:
    ML_$UNLOCK(resource_id);
}
