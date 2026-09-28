/*
 * win/do_io.c - WIN_$DO_IO (0x00E19776, 528 bytes)
 *
 * Jump-table entry +0x10, called by DISK_$DO_IO as do_io(vol, req, param_3,
 * result).  A format request goes to WIN_$FORMAT_TRACK; a read/write chain
 * is optionally sorted, then driven seek-by-seek from here with the
 * interrupt handler (WIN_$INT) carrying each transfer on to the next
 * request, and this routine waiting on unit 0's eventcount (8 clock ticks
 * at a time) and retrying on the recoverable statuses.
 *
 * Frame (link.w A6,-0x20; A5 A4 A3 A2 D6 D5 D4 D3 D2 saved), A5 = 0xE2B89C:
 *   A6-0x14  2  unit       always 0 (`clr.w (-0x14,A6)`)
 *   A6-0x12  2  lock       the module's lock word at +0x08
 *   A6-0x10  4  wait_val   unit 0's eventcount value + 1
 *   A6-0x04  4  local_req  DISK_$SORT's in/out cell
 *   A2          req, then &vol->dev_unit (dev_entry + 0x1C)
 *   A3          &TIME_$CLOCKH, A4 NULL (EC_$WAIT's other two entries)
 *   D2          status
 *   D4          DMA-overrun retries, D5 the dbf retry counter (0x17)
 *   D6          0xE2B0D4, loaded and never used
 */

#include "win/win_internal.h"
#include "parity/parity.h"

void WIN_$DO_IO(void *dev_entry, win_$request_t *req, void *param_3,
                int8_t *result)
{
    uint8_t *win_data = WIN_DATA_BASE;
    disk_$volume_t *vol = (disk_$volume_t *)dev_entry;
    uint16_t unit;                      /* A6-0x14 */
    int16_t lock;                       /* A6-0x12 */
    int32_t wait_val;                   /* A6-0x10 */
    win_$request_t *local_req;          /* A6-0x04 */
    win_$request_t *cur;                /* A0 */
    status_$t status;                   /* D2 */
    status_$t reinit_status;            /* D0 */
    uint16_t dma_retries;               /* D4 */
    int16_t retries;                    /* D5 */
    uint16_t op;                        /* D0 */

    (void)param_3;

    /* 0x00E19784-0x00E19796 */
    *result = 0;
    unit = 0;
    lock = *(int16_t *)(win_data + WIN_DEV_TYPE_OFFSET);

    /* 0x00E19798-0x00E197BE: a format request. */
    op = (uint16_t)(req->flags & 0x0F);
    if (op == 3) {
        ML_$LOCK(lock);
        WIN_$FORMAT_TRACK(dev_entry, req);
        goto unlock;
    }

    /* 0x00E197C2-0x00E197E0: a write chain of more than one request is
     * sorted first; DISK_$SORT hands the new head back through the cell. */
    if (op == 2 && req->next != 0) {
        local_req = req;
        DISK_$SORT(dev_entry, (void **)&local_req);
        req = local_req;
    }

    /* 0x00E197E4-0x00E19816 */
    ML_$LOCK(lock);
    WIN_CUR_REQ_VA = ARCH_PTR_TO_VA(req);
    WIN_DEV_INFO_VA = ARCH_PTR_TO_VA(dev_entry);
    dma_retries = 0;
    retries = 0x17;

again:
    /* 0x00E1981A-0x00E1983A: SEEK(0, vol->dev_unit, the current request,
     * flag 0) - a word result slot, popped with the arguments. */
    wait_val = WIN_UNIT_EC(0)->value + 1;
    status = SEEK(unit, vol->dev_unit,
                  ARCH_VA_TO_PTR(WIN_CUR_REQ_VA), 0);
    if (status == status_$ok) {
        /* 0x00E1983E-0x00E1984C */
        status = read_or_write_disk_record(unit);
    } else if (status == status_$disk_seek_error) {
        /* 0x00E1984E-0x00E1986A: re-initialise the drive (no result slot;
         * D0 is still the status); a non-zero one replaces the seek error.
         * Either way this counts as a retry. */
        reinit_status = win_$reinit_drive(unit, vol->dev_unit);
        if (reinit_status != status_$ok) {
            status = reinit_status;
        }
        goto next_retry;
    }

    /* 0x00E1986E-0x00E19870: a positive status is dispatched at once. */
    if (status > 0) {
        goto dispatch;
    }

    /*
     * 0x00E19872-0x00E198AE: wait for the interrupt handler to finish the
     * chain (unit 0's eventcount reaching wait_val) or for 8 clock ticks;
     * a timeout clears the seek-pending flag and posts the timeout status.
     * `clr.l D2` doubles as vals[2] and as the status, so the `tst.l D2`
     * at 0x00E198A6 never branches, and the module's status word is what
     * gets dispatched - zero means done.
     */
    status = 0;
    if (EC_$WAIT((ec_$wait_ecs_t){{ WIN_UNIT_EC(0),
                                    (ec_$eventcount_t *)&TIME_$CLOCKH,
                                    NULL }},
                 (ec_$wait_vals_t){{ wait_val,
                                     (int32_t)(TIME_$CLOCKH + 8),
                                     0 }}) != 0) {
        win_data[WIN_FLAG_OFFSET] = 0;
        *(status_$t *)(win_data + WIN_STATUS_OFFSET) =
            status_$disk_controller_timeout;
    }
    status = *(status_$t *)(win_data + WIN_STATUS_OFFSET);
    if (status == status_$ok) {
        goto unlock;
    }

dispatch:
    /* 0x00E198B2-0x00E198C4: a DMA overrun is retried up to 500 times on
     * its own counter, without touching the dbf budget. */
    if (status == status_$DMA_overrun) {
        dma_retries++;
        if (dma_retries < 0x1F4) {
            goto again;
        }
        goto fail;
    }

    /* 0x00E198C6-0x00E198F0: a parity error is real (fatal) when the
     * checker's low word is non-zero for the request's page and length. */
    if (status == status_$memory_parity_error_during_disk_write) {
        cur = (win_$request_t *)ARCH_VA_TO_PTR(WIN_CUR_REQ_VA);
        if ((PARITY_$CHK_IO(cur->pa >> 10, cur->length) & 0xFFFF) != 0) {
            goto fail;
        }
        goto next_retry;
    }

    /* 0x00E198F2-0x00E19902: a data check on a checksummed request (sign
     * bit of the flags byte) is fatal; otherwise it falls through the
     * remaining compares (none match) to the retry. */
    if (status == status_$disk_data_check) {
        cur = (win_$request_t *)ARCH_VA_TO_PTR(WIN_CUR_REQ_VA);
        if (cur->flags < 0) {
            goto fail;
        }
    }

    /* 0x00E19904-0x00E19926: not ready / unknown drive status / equipment
     * check re-initialise the drive first (result ignored). */
    if (status == status_$disk_not_ready ||
        status == status_$unknown_error_status_from_drive ||
        status == status_$disk_equipment_check) {
        (void)win_$reinit_drive(unit, vol->dev_unit);
    }

next_retry:
    /* 0x00E19928: `dbf D5w` with D5 = 0x17 - 23 retries, 24 attempts. */
    if (retries-- != 0) {
        goto again;
    }

fail:
    /* 0x00E1992C-0x00E1996E: with an error left, release the requesting
     * process (its pending byte), post the status to the current request,
     * and fail every request behind it with -1; the current-request cell
     * walks the chain and ends NULL. */
    if (status == status_$ok) {
        goto unlock;
    }
    cur = (win_$request_t *)ARCH_VA_TO_PTR(WIN_CUR_REQ_VA);
    WIN_IO_PENDING(cur->proc_id) = 0;
    cur->status = status;
    for (;;) {
        WIN_CUR_REQ_VA = cur->next;
        if (WIN_CUR_REQ_VA == 0) {
            break;
        }
        cur = (win_$request_t *)ARCH_VA_TO_PTR(WIN_CUR_REQ_VA);
        cur->status = (status_$t)-1;
    }

unlock:
    /* 0x00E19970-0x00E1997C */
    ML_$UNLOCK(lock);
}
