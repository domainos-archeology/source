/*
 * flp/format_track.c - FLP_FORMAT_TRACK (0x00E3DC78, 330 bytes)
 *
 * Formats the track a request names: builds the FDC's per-sector ID table
 * (C, H, R, N for each of the volume's sectors per track) in io_buffer,
 * seeks if necessary, points DMAC channel 3 at the table and issues FORMAT
 * TRACK.  Called only from FLP_DO_IO (0x00E3DE22), under its ML lock, and
 * with its A5 - this routine does not reload the module base.
 *
 * Frame (link.w A6,-0x20; A3 A2 D4 D3 D2 saved):
 *   D2          vol       argument 1
 *   D3          req       argument 2
 *   D0          loop count / unit-head word
 *   D1          sector number 1..n
 *   A1          walks io_buffer four bytes at a time (A5 + 4 + 4*i, fields
 *               at +0x7C..+0x7F, i.e. io_buffer[4*i..4*i+3])
 */

#include "flp/flp_internal.h"

void FLP_FORMAT_TRACK(disk_$volume_t *vol, disk_io_req_t *req)
{
    disk_device_entry_t *dev;           /* A1 */
    uint16_t i;                         /* D0 */
    uint16_t sector;                    /* D1 */
    uint8_t *entry;                     /* A0 */
    uint16_t unit;                      /* D1 */
    status_$t status;                   /* D0 */

    /* 0x00E3DC88-0x00E3DC9E: the register base from the controller's slot
     * (vol +0x18 -> device entry +0x06 -> ctlr * 8). */
    dev = (disk_device_entry_t *)vol->dev_info;
    FLP_DATA.hw_addr = FLP_DATA.ctlr_table[dev->controller].hw_addr;

    /*
     * 0x00E3DC9E-0x00E3DCD6: one 4-byte ID entry per sector (`dbf` with
     * count - 1, so sec_per_track iterations, skipped when it is zero):
     * the low byte of the request's cylinder word (req+5), its head byte
     * (req+6), the sector number counting from 1, and the low byte of
     * fmt_n (+0x103).
     */
    if (vol->sec_per_track != 0) {
        sector = 1;
        for (i = 0; i < vol->sec_per_track; i++) {
            entry = &FLP_DATA.io_buffer[4 * i];
            entry[0] = (uint8_t)((req->daddr >> 16) & 0xFF);
            entry[1] = (uint8_t)((req->daddr >> 8) & 0xFF);
            entry[2] = (uint8_t)sector;
            entry[3] = (uint8_t)(FLP_DATA.fmt_n & 0xFF);
            sector++;
        }
    }

    /* 0x00E3DCDA-0x00E3DCF4: no retries for a format; the FDC must be idle. */
    FLP_DATA.cmd_retry = 0;
    if ((FLP_REGS()->status & FLP_STATUS_CMD_MASK) != 0) {
        status = status_$disk_controller_busy;
        goto fail;
    }

    /* 0x00E3DCF8-0x00E3DD08: the FORMAT TRACK block's unit/head word. */
    FLP_DATA.fmt_cmd[1] = (uint16_t)(((req->daddr >> 8) & 0xFF) * 4
                                     + vol->dev_unit);

    /* 0x00E3DD0C-0x00E3DD40: seek with the separate SEEK block at +0x118
     * (three words, 0x00E3DDC2) when the unit is elsewhere, recording the
     * cylinder the FDC reports either way. */
    unit = vol->dev_unit;
    status = status_$ok;
    if (FLP_DATA.unit_cyl[unit] != (uint16_t)(req->daddr >> 16)) {
        FLP_DATA.seek_cmd[1] = FLP_DATA.fmt_cmd[1];
        FLP_DATA.seek_cmd[2] = (uint16_t)(req->daddr >> 16);
        status = EXCS(FLP_DATA.seek_cmd, &flp_word_three, vol);
        FLP_DATA.unit_cyl[unit] = FLP_$SREGS[1];
    }

    /* 0x00E3DD46-0x00E3DD48 */
    if (status != status_$ok) {
        goto fail;
    }

    /* 0x00E3DD4A-0x00E3DD7C: board control 3; DMAC channel 3 moves
     * (sec_per_track << 2) >> 1 words from the wired table (memory to
     * device, function code 1) and is started. */
    FLP_REGS()->control = 3;
    FLP_DMAC_MTC = (uint16_t)(((uint32_t)vol->sec_per_track << 2) >> 1);
    FLP_DMAC_MAR = FLP_DATA.fmt_buf_pa;
    FLP_DMAC_OCR = 0x12;
    FLP_DMAC_MFC = 1;
    FLP_DMAC_CCR = 0x80;

    /* 0x00E3DD82-0x00E3DD96: FORMAT TRACK, six words (0x00E3DDC4). */
    status = EXCS(FLP_DATA.fmt_cmd, &flp_word_six, vol);
    if (status == status_$ok) {
        return;
    }

fail:
    /* 0x00E3DD98-0x00E3DDB4: release the requesting process and record the
     * status in the request. */
    FLP_IO_PENDING(req->owner) = 0;
    req->status = status;
}
