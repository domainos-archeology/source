/*
 * flp/flp_do_io.c - FLP_DO_IO (0x00E3DDC6, 538 bytes)
 *
 * The body of the driver's DO_IO entry (FLP_$DO_IO in do_io.c is the gate).
 * Under the controller's ML lock it either hands a format request to
 * FLP_FORMAT_TRACK, or seeks (when the unit is not already on the
 * cylinder), programs DMAC channel 3 and issues READ DATA / WRITE DATA,
 * repeating the whole seek-and-transfer while EXCS answers FLP_$RETRY.
 *
 * Frame (link.w A6,-0x10; A5 A4 A3 A2 D4 D3 D2 saved):
 *   A4          vol       argument 1
 *   A3          req       argument 2
 *   (0x10,A6)   param_3   never read
 *   (0x14,A6)   zero      the word FLP_$DO_IO inserts; never read
 *   (0x16,A6)   result    argument 5, a byte cleared at entry
 *   D3          the controller's 8-byte slot in FLP_DATA (A5 + ctlr*8)
 *   D2          done      1 once a transfer completed
 *   D4          0xFFA000, the DMAC base
 *
 * Request fields (disk_io_req_t): daddr +0x04 is the cylinder word (+4),
 * head byte (+6) and sector byte (+7); ppn +0x14; owner +0x1E; op_flags
 * +0x1F whose low nibble is 1 = read, 2 = write, 3 = format.
 */

#include "flp/flp_internal.h"

void FLP_DO_IO(disk_$volume_t *vol, disk_io_req_t *req, void *param_3,
               int16_t zero, int8_t *result)
{
    disk_device_entry_t *dev;           /* A0 */
    flp_ctlr_entry_t *slot;             /* D3 */
    dcte_t *dcte;                       /* A2 */
    volatile flp_regs_t *regs;          /* A0 */
    status_$t status;                   /* D0 */
    uint16_t done;                      /* D2 */
    uint16_t op;                        /* D0 */
    uint16_t head;                      /* D0 */
    uint16_t unit;                      /* D1 */

    (void)param_3;
    (void)zero;

    /* 0x00E3DDD4-0x00E3DDF4: the controller number comes from the volume's
     * device entry (+0x18 -> +0x06); its slot supplies the register base. */
    dev = (disk_device_entry_t *)vol->dev_info;
    slot = &FLP_DATA.ctlr_table[dev->controller];
    FLP_DATA.hw_addr = slot->hw_addr;

    /* 0x00E3DDFA-0x00E3DDFE */
    *result = 0;

    /* 0x00E3DE00-0x00E3DE10: ML_$LOCK on the word at +0x3C of the DCTE
     * this controller registered with (a word result slot is reserved and
     * discarded). */
    dcte = (dcte_t *)ARCH_VA_TO_PTR(slot->dcte_va);
    ML_$LOCK((int16_t)(dcte->disk_error_que >> 16));

    /* 0x00E3DE12-0x00E3DE28: a format request is a separate routine. */
    op = req->op_flags & 0x0F;
    if (op == 3) {
        FLP_FORMAT_TRACK(vol, req);
        goto unlock;
    }

    /* 0x00E3DE2C-0x00E3DE46: a write to a unit whose disk has changed is
     * refused. */
    if (op == 2) {
        if (FLP_DATA.disk_change[vol->dev_unit] < 0) {
            status = status_$storage_module_stopped;
            goto fail;
        }
    }

    /* 0x00E3DE4A-0x00E3DE5E: retry budgets, the transfer page, done = 0. */
    FLP_DATA.cmd_retry = 0x19;
    FLP_DATA.dma_retry = 0x1F4;
    FLP_DATA.buf_pa = req->ppn;
    done = 0;

again:
    /* 0x00E3DE64-0x00E3DE7A: the FDC must be idle. */
    regs = FLP_REGS();
    if ((regs->status & FLP_STATUS_CMD_MASK) != 0) {
        status = status_$disk_controller_busy;
        goto fail;
    }

    /* 0x00E3DE7E-0x00E3DEA4: fill the unit/head, cylinder, head and sector
     * words of the READ/WRITE DATA block.  The sector is the request's
     * sector byte + done + 1. */
    head = (req->daddr >> 8) & 0xFF;
    FLP_DATA.rw_cmd[1] = (uint16_t)(head * 4 + vol->dev_unit);
    FLP_DATA.rw_cmd[2] = (uint16_t)(req->daddr >> 16);
    FLP_DATA.rw_cmd[3] = head;
    FLP_DATA.rw_cmd[4] = (uint16_t)((req->daddr & 0xFF) + done + 1);

    /* 0x00E3DEA8-0x00E3DED6: seek if the unit is on another cylinder - the
     * first three words of the block are the SEEK command (0x0F), three
     * words (0x00E3DDC2).  The cylinder the FDC reports (sregs[1], PCN) is
     * recorded whether or not the seek succeeded. */
    unit = vol->dev_unit;
    status = status_$ok;
    if (FLP_DATA.unit_cyl[unit] != (uint16_t)(req->daddr >> 16)) {
        FLP_DATA.rw_cmd[0] = 0x0F;
        status = EXCS(FLP_DATA.rw_cmd, &flp_word_three, vol);
        FLP_DATA.unit_cyl[unit] = FLP_$SREGS[1];
    }

    /* 0x00E3DEDC-0x00E3DEDE */
    if (status == status_$ok) {
        /* 0x00E3DEE2-0x00E3DF04: board control 2 for a read, 3 otherwise. */
        op = req->op_flags & 0x0F;
        if (op == 1) {
            FLP_REGS()->control = 2;
        } else {
            FLP_REGS()->control = 3;
        }

        /* 0x00E3DF04-0x00E3DF32: DMAC channel 3: 0x200 transfers from/to
         * the page (ppn << 10); OCR 0x12 (memory to device) for a write,
         * 0x92 (device to memory) for a read; function code 1; start. */
        FLP_DMAC_MTC = 0x200;
        FLP_DMAC_MAR = req->ppn << 10;
        if (op == 2) {
            FLP_DMAC_OCR = 0x12;
        } else {
            FLP_DMAC_OCR = 0x92;
        }
        FLP_DMAC_MFC = 1;
        FLP_DMAC_CCR = 0x80;

        /* 0x00E3DF38-0x00E3DF4C: READ DATA = base + 6 (0x46), WRITE DATA =
         * base + 5 (0x45). */
        if (op == 1) {
            FLP_DATA.rw_cmd[0] = (uint16_t)(FLP_DATA.base_cmd + 6);
        } else {
            FLP_DATA.rw_cmd[0] = (uint16_t)(FLP_DATA.base_cmd + 5);
        }

        /* 0x00E3DF50-0x00E3DF66: nine words (0x00E3DFE0). */
        status = EXCS(FLP_DATA.rw_cmd, &flp_word_nine, vol);
        if (status == status_$ok) {
            done++;
        }
    }

    /* 0x00E3DF68-0x00E3DF7C: nothing done and no error means go round
     * again; FLP_$RETRY from EXCS does too. */
    if (done == 0 && status == status_$ok) {
        status = FLP_$RETRY;
    }
    if (status == FLP_$RETRY) {
        goto again;
    }

    /* 0x00E3DF80-0x00E3DF92: a disk change during the transfer fails it. */
    if (status == status_$ok) {
        if (FLP_DATA.disk_change[vol->dev_unit] < 0) {
            status = status_$storage_module_stopped;
        }
    }

    /* 0x00E3DF98-0x00E3DFA4 */
    FLP_REGS()->control = 3;
    if (status == status_$ok) {
        goto unlock;
    }

fail:
    /* 0x00E3DFA6-0x00E3DFC0: release the requesting process (its pending
     * byte in DISK_$DATA) and record the status in the request. */
    FLP_IO_PENDING(req->owner) = 0;
    req->status = status;

unlock:
    /* 0x00E3DFC4-0x00E3DFD0: the slot's DCTE is re-read for the unlock. */
    dcte = (dcte_t *)ARCH_VA_TO_PTR(slot->dcte_va);
    ML_$UNLOCK((int16_t)(dcte->disk_error_que >> 16));
}
