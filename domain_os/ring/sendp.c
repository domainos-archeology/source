/*
 * RING_$SENDP - Token ring packet transmission
 *
 * This file contains the packet transmission function for the ring module.
 * It handles DMA setup, congestion control, retries, and error reporting.
 */

#include "ring/ring_internal.h"
#include "misc/crash_system.h"
#include "time/time.h"
#include "mmu/mmu.h"

/*
 * Hardware status register bits
 */
#define RING_HW_STATUS_BUSY         0x2000
#define RING_HW_STATUS_COMPLETE     0x0014
#define RING_HW_STATUS_BIPHASE      0x0800
#define RING_HW_STATUS_ESB          0x0400
#define RING_HW_STATUS_PARITY       0x0040
#define RING_HW_STATUS_NOTACK       0x0001
#define RING_HW_STATUS_DELAYED      0x0220
#define RING_HW_STATUS_NORESP       0x0080
#define RING_HW_STATUS_CONGESTED    0x0018
#define RING_HW_STATUS_ACCEPTED     0x0004
#define RING_HW_STATUS_REJECT       0x0002
#define RING_HW_STATUS_NOTOKEN      0x0010

/*
 * RING_$SENDP - Send a packet on the token ring
 *
 * Transmits a packet on the specified ring unit. Handles:
 *   - Hardware busy/congestion conditions
 *   - DMA setup for header and data
 *   - Retry logic for transient failures
 *   - Statistics updates
 *
 * Original address: 0x00E75916
 *
 * Assembly analysis:
 *   - param_1 is a pointer to unit number (short)
 *   - Multiplies unit by 0x244 for unit data offset
 *   - Multiplies unit by 0x3C for stats offset
 *   - Checks initialized flag at +0x60 in unit data
 *   - Gets hw_regs pointer from +0x1C in unit data
 *   - Implements retry loop with congestion backoff
 *
 * @param unit_ptr      Pointer to unit number
 * @param hdr_pa        Header physical address
 * @param hdr_va        Header virtual address (contains header data)
 * @param data_info     Data buffer info (8 bytes: PA + length info)
 * @param data_len      Data length
 * @param send_flags    Send flags (input/output)
 * @param result_flags  Output: result flags (2 bytes)
 * @param status_ret    Output: Status code
 */
void RING_$SENDP(uint16_t *unit_ptr, uint32_t hdr_pa, void *hdr_va,
                 uint64_t data_info, uint16_t data_len,
                 uint16_t *send_flags, uint16_t *result_flags,
                 status_$t *status_ret)
{
    uint16_t unit;
    ring_unit_t *unit_data;
    ring_$stats_t *stats;
    int16_t *hw_regs;
    int32_t tx_ec_val;
    status_$t local_status;
    clock_t abs_time;
    clock_t timeout;
    clock_t deadline;
    uint16_t delay_type;
    uint16_t hw_status;
    int16_t retry_count;
    int8_t success = 0;
    int8_t force_start = 0;
    uint16_t local_data_len;
    uint32_t local_data_pa;
    uint32_t *data_info_ptr;
    uint32_t data_buf[4];  /* Local copy of data info */
    uint8_t *result_bytes = (uint8_t *)result_flags;

    unit = *unit_ptr;
    unit_data = &RING_$DATA.units[unit];
    stats = &RING_$STATS[unit];

    /* Clear result flags */
    result_bytes[0] = 0;
    result_bytes[1] = 0;

    /*
     * Check if unit is initialized.
     * The initialized flag is at offset 0x60 and is -1 when initialized.
     */
    if (unit_data->initialized >= 0) {
        /*
         * Unit not initialized - cannot transmit.
         * 0x00E7595E `bmi` falls through to 0x00E75960 `bset.b #0x4,(0x1,A3)`
         * and 0x00E7596A `move.l #0x100002,(A4)`, then branches straight to
         * the epilogue at 0x00E75DD6 - skipping the transmit_done stores.
         */
        result_bytes[1] |= 0x10;
        *status_ret = status_$io_controller_not_in_system;
        return;
    }

    /*
     * Check data length against maximum.
     */
    if (data_len > RING_$DATA.max_data_len) {
        *status_ret = status_$network_data_length_too_large;
        return;
    }

    /* Default to transmit failed */
    *status_ret = status_$network_transmit_failed;

    /* Get hardware registers */
    hw_regs = (int16_t *)unit_data->hw_regs;

    /*
     * Check if hardware is busy.
     * hw_regs[3] & 0x2000 indicates busy state.
     */
    if ((hw_regs[3] & RING_HW_STATUS_BUSY) == 0) {
        /* Hardware is busy - set flag */
        result_bytes[1] |= 0x10;
        return;
    }

    /*
     * Retry budget.  0x00E759AA `moveq #0x14,D6`; 0x00E759AC
     * `tst.b (0x34,A2)` / `bpl` skips 0x00E759B2 `moveq #0xa,D6`, so the
     * budget drops to 10 only when the PREVIOUS send finished without a hard
     * error (last_success, +0x34).
     */
    retry_count = 0x14;  /* 20 retries default */
    if (stats->last_success < 0) {
        retry_count = 10;
    }

    /* 0x00E759CE `addq.l #0x1,(0x2,A2)` and 0x00E759D2
     * `move.b D4b,(0x3a,A2)` with D4b still zero. */
    stats->xmit_call++;
    stats->retry_pending = 0;

    /*
     * Setup local data buffer info.
     * data_info is 64 bits containing PA and other info.
     */
    data_info_ptr = (uint32_t *)&data_info;
    if (data_len == 0) {
        local_data_len = 0;
        local_data_pa = hdr_pa;
    } else {
        local_data_len = data_len;
        local_data_pa = data_info_ptr[0];
        data_buf[0] = data_info_ptr[0];
        data_buf[1] = data_info_ptr[1];
    }

    /*
     * Calculate header checksum if enabled.
     */
    if (NETWORK_$DO_CHKSUM < 0) {
        /*
         * 0x00E759FE-0x00E75A12: `pea (0x14,A6)` gives HDR_CHKSUM the ADDRESS
         * of the caller's fourth argument, and the callee reads its first
         * WORD as the header byte count -- on m68k that is the top 16 bits of
         * data_info.  The width is spelled out rather than taking the address
         * of the 64-bit local, which would read the wrong half on a
         * little-endian host.  (source-bwuv covers this frame's argument
         * model, which is still approximate.)
         */
        uint16_t hdr_len_w = (uint16_t)(data_info >> 48);

        *((uint8_t *)hdr_va + 0xd) = HDR_CHKSUM(hdr_va, &hdr_len_w);
    } else {
        *((uint8_t *)hdr_va + 0xd) = 1;
    }

    /*
     * Main transmit loop with retry logic.
     */
transmit_retry:
    /*
     * Get current transmit event count + 1.
     * We'll wait for this value to detect completion.
     */
    tx_ec_val = unit_data->tx_ec.value + 1;

    /*
     * Setup transmit DMA.
     * Header length is in the low 16 bits of hdr_va data.
     */
    ring_$setup_tx_dma(hdr_pa, (int16_t)(data_info & 0xFFFF),
                       local_data_pa, local_data_len);

    /*
     * Choose the transmission mode.  0x00E75A42 `tst.b (0x3a,A2)` / `bmi.w`
     * selects the force-start arm at 0x00E75B24.
     */
    if (stats->retry_pending < 0) {
        /*
         * Congested mode - use force start with timeout.
         */
        TIME_$ABS_CLOCK(&abs_time);

        /* Start transmit with force */
        hw_regs[0] = 0x7000;
        force_start = -1;

        /*
         * Wait for completion or timeout.
         */
        delay_type = 0;  /* Relative delay */
        int32_t local_ec_val = tx_ec_val;
        int8_t wait_result = TIME_$WAIT2(&delay_type, &RING_$DATA.xmit_timeout1,
                                         &unit_data->tx_ec, (uint32_t *)&local_ec_val,
                                         &local_status);

        if (wait_result >= 0) {
            /* Timeout - try to abort */
            hw_regs[0] = 0x4000;

            if ((hw_regs[0] & RING_HW_STATUS_BUSY) == 0) {
                goto check_completion;
            }

            /* Still busy - wait more */
            local_ec_val = tx_ec_val;
            TIME_$WAIT2(&delay_type, &RING_$DATA.xmit_timeout2,
                       &unit_data->tx_ec, (uint32_t *)&local_ec_val, &local_status);

            if ((hw_regs[0] & RING_HW_STATUS_BUSY) != 0) {
                /* Still busy after the second wait: 0x00E75BAA `bne` to
                 * 0x00E75BCC `move.l #0x31000e,(A4)`. */
                *status_ret = status_$ring_controller_hardware_error;
                goto transmit_done;
            }
        }
    } else {
        /*
         * Normal mode - start transmit and poll.
         */
        hw_regs[0] = 0x6000;

        /* Get absolute time for deadline calculation */
        TIME_$ABS_CLOCK(&abs_time);
        deadline = abs_time;

        /*
         * Poll for completion.
         *
         * TODO(source-bwuv): the deadline test is missing.  The image spins at
         * 0x00E75A78-0x00E75AA2: `cmp.l (0x10,A1),D7` / `ble 0x00E75C18`, then
         * TIME_$ABS_CLOCK into A6-0x18 and SUB48(A6-0x20, A6-0x18) at
         * 0x00E75A98 with `tst.b D0b` / `bpl 0x00E75A78`, i.e. it loops until
         * the 48-bit deadline seeded at 0x00E75A66 from RING_$DATA+0x590 has
         * passed.  Everything from here to 0x00E75B72 is still a sketch.
         */
        do {
            if (tx_ec_val <= unit_data->tx_ec.value) {
                goto process_status;
            }
            TIME_$ABS_CLOCK(&timeout);
            break;
        } while (1);

        /* Polling timed out - switch to wait mode */
        RING_$XMIT_WAITED++;

        delay_type = 0;
        int32_t local_ec_val2 = tx_ec_val;
        int8_t wait_result = TIME_$WAIT2(&delay_type, &RING_$DATA.wait_timeout,
                                         &unit_data->tx_ec, (uint32_t *)&local_ec_val2,
                                         &local_status);
        if (wait_result >= 0) {
            /* Timeout - retry with force start */
            force_start = -1;
            goto transmit_retry;
        }
    }

check_completion:
    if (tx_ec_val <= unit_data->tx_ec.value) {
        goto process_status;
    }

    /* Clear DMA channel and check for retry */
    ring_$clear_dma_channel(2, unit);

    /*
     * 0x00E75BC6 `tst.b (0x3a,A2)` / `bpl` - a retry that was already pending
     * means the controller never came back, so report it.  This arm shares
     * 0x00E75BCC with the "still busy" exit above.
     */
    if (stats->retry_pending < 0) {
        *status_ret = status_$ring_controller_hardware_error;
        goto transmit_done;
    }

    /* 0x00E75BDA `tst.b D4b` / `bmi.w 0x00E75DC2`. */
    if (force_start < 0) {
        goto transmit_done;
    }

    /* 0x00E75BE0 `addq.w #0x1,(0x1a,A2)`. */
    stats->xmit_tim++;

    /*
     * 0x00E75BE4-0x00E75BEE: `move.b (0x36,A2),D0b` / `not.b D0b` /
     * `and.b (0x38,A2),D0b` / `bpl` - enter degraded mode the first time
     * biphase_flag is true while congestion_flag is still false.
     */
    if ((int8_t)(~stats->congestion_flag & stats->biphase_flag) < 0) {
        stats->congestion_flag = -1;            /* 0x00E75BF0 `st (0x36,A2)` */
        result_bytes[0] |= 0x08;                /* 0x00E75BF4 `bset.b #0x3,(A3)` */

        /* Reconfigure hardware mode (0x00E75BF8 `move.w (0x6,A4),D0w` /
         * `btst.l #0xe,D0`). */
        if ((hw_regs[3] & 0x4000) == 0) {
            hw_regs[3] = 0x2800;
        } else {
            hw_regs[3] = 0x6800;
        }
    }

    /* 0x00E75C10 `st (0x3a,A2)` then 0x00E75C14 `bra.w 0x00E75A22` - this arm
     * always retries; it never falls into the status decode. */
    stats->retry_pending = -1;
    goto transmit_retry;

process_status:
    /*
     * Switch to CPU access mode for reading status.
     */
    MMU_$MCR_CHANGE(4);

    hw_status = hw_regs[0];

    /* Clear DMA channel */
    ring_$clear_dma_channel(2, unit);

    /*
     * Check for successful completion (status == 0x14).
     */
    if (hw_status == RING_HW_STATUS_COMPLETE) {
        result_bytes[0] |= 0x80;  /* Success flag */
        stats->xmitcnt++;
        success = -1;
        NETWORK_$ACTIVITY_FLAG = -1;
        goto transmit_done;
    }

    /*
     * Initialize timeout for potential retry.
     */
    timeout.high = 0;
    timeout.low = 500;

    /*
     * Check for biphase/ESB errors.
     */
    if ((hw_status & (RING_HW_STATUS_BIPHASE | RING_HW_STATUS_ESB)) != 0) {
        if ((hw_status & RING_HW_STATUS_BIPHASE) != 0) {
            result_bytes[1] |= 0x20;
            RING_$XMIT_BIPHASE++;
        }
        if ((hw_status & RING_HW_STATUS_ESB) != 0) {
            result_bytes[1] |= 0x04;
            RING_$XMIT_ESB++;
        }
        stats->xmit_modem++;
    }

    /*
     * Check for parity error.
     */
    if ((hw_status & RING_HW_STATUS_PARITY) != 0) {
        /* Parity error - check if address comparison matches */
        /* This involves PARITY_$CHK_IO call in original */
        result_bytes[1] |= 0x80;
        stats->xmit_bus++;
        goto retry_check;
    }

    /*
     * Check for not-acknowledged.
     */
    if ((hw_status & RING_HW_STATUS_NOTACK) != 0) {
        result_bytes[0] |= 0x01;
        stats->xmit_orun++;
        goto retry_check;
    }

    /*
     * Check for delayed response.
     */
    if ((hw_status & RING_HW_STATUS_DELAYED) != 0) {
        result_bytes[0] |= 0x04;
        stats->xmit_nortn++;
        goto retry_or_done;
    }

    /*
     * Check for no response.
     */
    if ((hw_status & RING_HW_STATUS_NORESP) != 0) {
        result_bytes[1] |= 0x40;
        stats->xmit_apar++;
        goto retry_or_done;
    }

    /*
     * Mark as successful for remaining cases.
     */
    success = -1;
    NETWORK_$ACTIVITY_FLAG = -1;

    /*
     * Check for congestion (both bits 3 and 4 set).
     */
    if ((hw_status & RING_HW_STATUS_CONGESTED) == RING_HW_STATUS_CONGESTED) {
        result_bytes[1] |= 0x40;
        stats->xmit_error++;
        goto retry_or_done;
    }

    /*
     * Check for accepted.
     */
    if ((hw_status & RING_HW_STATUS_ACCEPTED) != 0) {
        result_bytes[0] |= 0x80;
        stats->xmitcnt++;
        *status_ret = status_$ok;
        goto transmit_done;
    }

    /*
     * Check for reject.
     */
    if ((hw_status & RING_HW_STATUS_REJECT) != 0) {
        result_bytes[0] |= 0x40;
        stats->xmit_wack++;
        timeout.low = 500;
        goto retry_check;
    }

    /*
     * Check for no token.
     */
    if ((hw_status & RING_HW_STATUS_NOTOKEN) != 0) {
        /* Check bit 3 also */
        if ((hw_status & 0x08) == 0) {
            RING_$UNEXPECTED_XMIT_STAT = hw_status;
        }
        result_bytes[1] |= 0x40;
        goto retry_or_done;
    }

    /*
     * Other status - check destination type.
     */
    result_bytes[0] |= 0x20;
    if (*send_flags == 0 || *send_flags > 4) {
        stats->xmit_nack++;
        timeout.low = 250;
        goto retry_check;
    }

    /* Success */
    stats->xmitcnt++;
    *status_ret = status_$ok;
    goto transmit_done;

retry_or_done:
    if (success < 0) {
        goto transmit_done;
    }

retry_check:
    retry_count--;
    if (retry_count == 0) {
        goto transmit_done;
    }

    /* Check if destination allows retry (bit 1 of byte 4 in send_flags) */
    if ((((uint8_t *)send_flags)[4] & 0x02) != 0) {
        goto transmit_done;
    }

    /* 0x00E75DA4 `clr.b (0x3a,A2)`. */
    stats->retry_pending = 0;

    /* Wait before retry */
    delay_type = 0;
    TIME_$WAIT(&delay_type, &timeout, &local_status);

    goto transmit_retry;

transmit_done:
    /*
     * 0x00E75DC2 `not.b D5b` is UNCONDITIONAL, and 0x00E75DC4
     * `move.b D5b,(0x34,A2)` sets the condition codes the following `bpl`
     * tests.  D5 is set only by the two `st D5b` sites (0x00E75C46 and
     * 0x00E75D0A), so last_success ends up true exactly when neither fired.
     */
    success = (int8_t)~success;
    stats->last_success = success;

    if (success < 0) {
        result_bytes[0] |= 0x02;    /* 0x00E75DCA `bset.b #0x1,(A3)` */
        /*
         * 0x00E75DCE `clr.b (0x38,A2)` is dead in the original: the very next
         * instruction stores D4 over it on both paths.  Preserved as written.
         */
        stats->biphase_flag = 0;
    }

    /* 0x00E75DD2 `move.b D4b,(0x38,A2)` - the force-start flag, not a byte of
     * send_flags. */
    stats->biphase_flag = force_start;
}
