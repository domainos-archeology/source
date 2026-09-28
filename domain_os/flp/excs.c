/*
 * flp/excs.c - EXCS (0x00E3E268, 566 bytes)
 *
 * Execute an FDC command and interpret its outcome: write the command
 * words, wait for FLP_$INT to advance FLP_$EC (or for eight clock ticks),
 * check the DMA and parity state, and turn the result registers into a
 * status.  A status of FLP_$RETRY tells FLP_DO_IO to issue the command
 * again; cmd_retry / dma_retry in FLP_DATA are the budgets.
 *
 * Frame (link.w A6,-0x14; A5 A3 A2 D4 D3 D2 saved):
 *   A6-0x0E  2  st3       SENSE DRIVE STATUS's result word
 *   A6-0x08  4  target    FLP_$EC.value + 1, sampled before the command
 *   A3          cmd       argument 1
 *   A2          vol       argument 3
 *   D2          result    the status being built
 *   D3          which     EC_$WAIT's index (0 = FLP_$EC, 1 = the clock),
 *                         later the unit number
 *   D4          scratch
 *
 * Result-register bits (FLP_$SREGS, one FDC byte per word, so the low-byte
 * `btst.b` in the image are bits of the word):
 *   sregs[0] = ST0: 0x10 equipment check, 0x08 not ready, 0xC0 interrupt code
 *   sregs[1] = ST1: 0x02 not writable, 0x85 end-of-cyl/no-data/missing-AM,
 *                   0x20 data error, 0x10 overrun
 *   sregs[2] = ST2: 0x10 wrong cylinder
 */

#include "flp/flp_internal.h"

status_$t EXCS(uint16_t *cmd, int16_t *count_ptr, disk_$volume_t *vol)
{
    uint16_t st3;                       /* A6-0x0E */
    int32_t target;                     /* A6-0x08 */
    status_$t status;                   /* D0 */
    status_$t result;                   /* D2 */
    int16_t which;                      /* D3 */
    uint16_t unit;                      /* D3, from 0x00E3E414 */
    volatile flp_regs_t *regs;          /* A0 */

    /* 0x00E3E27E-0x00E3E284 */
    target = FLP_$EC.value + 1;

    /* 0x00E3E288-0x00E3E29C: write the command (direction 1 at 0x00E3E110,
     * pea (-0x17a,PC)); a handshake failure is returned as is. */
    status = SHAKE(cmd, count_ptr, &flp_word_one);
    if (status != status_$ok) {
        result = status;                            /* 0x00E3E32A */
        return result;
    }

    /*
     * 0x00E3E2A0-0x00E3E2CA: EC_$WAIT on FLP_$EC reaching target or
     * TIME_$CLOCKH reaching now + 8; the third slot is NULL / 0.  The
     * 0-based index comes back in D0.
     */
    which = EC_$WAIT((ec_$wait_ecs_t){{ &FLP_$EC,
                                        (ec_$eventcount_t *)&TIME_$CLOCKH,
                                        NULL }},
                     (ec_$wait_vals_t){{ target, TIME_$CLOCKH + 8, 0 }});

    /* 0x00E3E2CC-0x00E3E2D6: the DMA / parity checks are skipped for a
     * RECALIBRATE (command code 7). */
    result = 0;
    if ((cmd[0] & 7) != 7) {
        regs = FLP_REGS();
        /* 0x00E3E2D8-0x00E3E304: with bit 1 of the register word at +6 set,
         * ask the parity checker about the transfer page; a non-zero low
         * word (`tst.w D0w` / `sne`) is a parity error during the write. */
        if ((regs->w_06 & 0x0002) != 0) {
            if ((PARITY_$CHK_IO(1, FLP_DATA.buf_pa) & 0xFFFF) != 0) {
                result = status_$memory_parity_error_during_disk_write;
                return result;
            }
        }
        /* 0x00E3E308-0x00E3E32C: channel 3's DMA status; "not at end of
         * range" is fine, anything else is retried while cmd_retry lasts,
         * or returned. */
        status = DMA_$CHECK(FLP_DMA_CHANNEL);
        if (status != status_$ok && status != status_$dma_not_at_end_of_range) {
            if (FLP_DATA.cmd_retry != 0) {
                goto retry;                         /* 0x00E3E326 bne 0x00E3E488 */
            }
            result = status;                        /* 0x00E3E32A */
            return result;
        }
    }

    /* 0x00E3E330-0x00E3E334: a wake-up by the clock rather than the
     * interrupt is recorded as ST0 = equipment check. */
    if (which != 0) {
        FLP_$SREGS[0] = 0x10;
    }

    /* 0x00E3E33A-0x00E3E342: nothing of interest in ST0 - success. */
    if ((FLP_$SREGS[0] & 0xD8) == 0) {
        return result;
    }

    /* 0x00E3E346-0x00E3E37E: SENSE DRIVE STATUS for the command's unit/head
     * word (two words written, 0x00E3E21C / 0x00E3E110), then one word read
     * (0x00E3E110 / 0x00E3E10E).  st3 is loaded into D1 before the read's
     * status is tested. */
    FLP_DATA.sense_cmd[1] = cmd[1];
    status = SHAKE(FLP_DATA.sense_cmd, &flp_word_two, &flp_word_one);
    if (status == status_$ok) {
        status = SHAKE(&st3, &flp_word_one, &flp_word_zero);
        if (status == status_$ok) {
            goto interpret;
        }
    }
    result = status;                                /* 0x00E3E380 */
    goto tail;

interpret:
    /* 0x00E3E386-0x00E3E394: ST0 bit 4 */
    if ((FLP_$SREGS[0] & 0x10) != 0) {
        result = status_$disk_equipment_check;
        goto tail;
    }

    /* 0x00E3E398-0x00E3E3C8: ST0 bit 3 - not ready.  The retry budget is
     * dropped; a two-sided request (unit/head word >= 4) on a drive whose
     * ST3 says ready (bit 3 clear) and two-sided (bit 5 set) is reported
     * as "floppy is not 2-sided" instead. */
    if ((FLP_$SREGS[0] & 0x08) != 0) {
        FLP_DATA.cmd_retry = 0;
        if ((st3 & 0x08) == 0 && (st3 & 0x20) != 0 && cmd[1] >= 4) {
            result = status_$floppy_is_not_2_sided;
        } else {
            result = status_$disk_not_ready;
        }
        goto tail;
    }

    /* 0x00E3E3CC-0x00E3E3DE: interrupt code 11 (both ST0 bits 7:6 set,
     * `not.w` then `andi.w #0xc0` leaves zero) - drive not ready. */
    if (((uint16_t)~FLP_$SREGS[0] & 0xC0) == 0) {
        result = status_$disk_not_ready;
        goto clear_retry;
    }

    /* 0x00E3E3E0-0x00E3E3EE: ST1 bit 1 */
    if ((FLP_$SREGS[1] & 0x02) != 0) {
        result = status_$disk_write_protected;
        goto clear_retry;
    }

    /* 0x00E3E3F0-0x00E3E446: ST1 end of cylinder / no data / missing
     * address mark - bad format.  If ST2 says wrong cylinder and retries
     * remain, spend them all on one RECALIBRATE of the unit (recursive
     * EXCS, two words at 0x00E3E21C) and forget its cylinder. */
    if ((FLP_$SREGS[1] & 0x85) != 0) {
        result = status_$bad_disk_format;
        if ((FLP_$SREGS[2] & 0x10) == 0) {
            goto tail;
        }
        if (FLP_DATA.cmd_retry == 0) {
            goto tail;
        }
        FLP_DATA.cmd_retry = 1;
        unit = cmd[1] & 3;
        FLP_DATA.recal_cmd[1] = unit;
        status = EXCS(FLP_DATA.recal_cmd, &flp_word_two, vol);
        FLP_DATA.unit_cyl[unit] = 0;                /* 0x00E3E430-0x00E3E438 */
        if (status == status_$ok) {
            goto tail;
        }
        result = status;
        goto clear_retry;
    }

    /* 0x00E3E448-0x00E3E45E: ST1 bit 5 - data check.  With bit 1 of the
     * volume's option byte at +0x29 set it is returned at once, otherwise
     * it goes through the retry tail. */
    if ((FLP_$SREGS[1] & 0x20) != 0) {
        result = status_$disk_data_check;
        if ((vol->as_options & 0x0002) != 0) {
            return result;
        }
        goto tail;
    }

    /* 0x00E3E460-0x00E3E47A: ST1 bit 4 - overrun; retried from the DMA
     * budget (bypassing cmd_retry), else reported. */
    if ((FLP_$SREGS[1] & 0x10) != 0) {
        if (FLP_DATA.dma_retry > 0) {
            FLP_DATA.dma_retry--;
            goto retry_marker;                      /* 0x00E3E472 bra 0x00E3E48C */
        }
        result = status_$DMA_overrun;
        return result;
    }

    /* 0x00E3E47C */
    result = status_$unknown_status_returned_by_hardware;
    goto tail;

clear_retry:
    /* 0x00E3E442 */
    FLP_DATA.cmd_retry = 0;

tail:
    /* 0x00E3E482-0x00E3E486: with retries left, spend one and ask for a
     * retry instead of reporting the error. */
    if (FLP_DATA.cmd_retry == 0) {
        return result;
    }
retry:
    /* 0x00E3E488 */
    FLP_DATA.cmd_retry--;
retry_marker:
    /* 0x00E3E48C-0x00E3E492 */
    result = FLP_$RETRY;
    return result;
}
