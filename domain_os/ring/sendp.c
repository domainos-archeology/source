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
        /* Unit not initialized - cannot transmit */
        *status_ret = status_$ok;
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
     * Get retry count from stats.
     * If congestion flag is set, use shorter retry count.
     */
    retry_count = 0x14;  /* 20 retries default */
    if (stats->congestion_flag < 0) {
        retry_count = 10;
    }

    /* Increment transmit count */
    stats->xmit_count++;
    stats->congestion_flag = 0;

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
        *((uint8_t *)hdr_va + 0xd) = HDR_CHKSUM(hdr_va, &data_info);
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
     * Check congestion state and choose transmission mode.
     */
    if (stats->congestion_flag < 0) {
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
                /* Hardware error - timeout */
                *status_ret = status_$ring_request_denied;
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
         */
        do {
            if (tx_ec_val <= unit_data->tx_ec.value) {
                goto process_status;
            }
            TIME_$ABS_CLOCK(&timeout);
            /* Check if deadline passed - simplified */
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

    if (stats->congestion_flag >= 0) {
        if (force_start < 0) {
            goto transmit_done;
        }
        /* Update retry stats and flags */
        stats->retry_count++;

        /* Check if should set congestion flag */
        if ((stats->biphase_flag & ~stats->_reserved2) & 0x80) {
            stats->_reserved2 = -1;
            result_bytes[0] |= 0x08;

            /* Reconfigure hardware mode */
            if ((hw_regs[3] & 0x4000) == 0) {
                hw_regs[3] = 0x2800;
            } else {
                hw_regs[3] = 0x6800;
            }
        }

        stats->congestion_flag = -1;
        goto transmit_retry;
    }

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
        stats->success_count++;
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
        stats->biphase_count++;
    }

    /*
     * Check for parity error.
     */
    if ((hw_status & RING_HW_STATUS_PARITY) != 0) {
        /* Parity error - check if address comparison matches */
        /* This involves PARITY_$CHK_IO call in original */
        result_bytes[1] |= 0x80;
        stats->parity_count++;
        goto retry_check;
    }

    /*
     * Check for not-acknowledged.
     */
    if ((hw_status & RING_HW_STATUS_NOTACK) != 0) {
        result_bytes[0] |= 0x01;
        stats->abort_count++;
        goto retry_check;
    }

    /*
     * Check for delayed response.
     */
    if ((hw_status & RING_HW_STATUS_DELAYED) != 0) {
        result_bytes[0] |= 0x04;
        stats->delayed_count++;
        goto retry_or_done;
    }

    /*
     * Check for no response.
     */
    if ((hw_status & RING_HW_STATUS_NORESP) != 0) {
        result_bytes[1] |= 0x40;
        stats->noresp_count++;
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
        stats->unexpected_count++;
        goto retry_or_done;
    }

    /*
     * Check for accepted.
     */
    if ((hw_status & RING_HW_STATUS_ACCEPTED) != 0) {
        result_bytes[0] |= 0x80;
        stats->success_count++;
        *status_ret = status_$ok;
        goto transmit_done;
    }

    /*
     * Check for reject.
     */
    if ((hw_status & RING_HW_STATUS_REJECT) != 0) {
        result_bytes[0] |= 0x40;
        stats->collision_count++;
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
        stats->no_response_count++;
        timeout.low = 250;
        goto retry_check;
    }

    /* Success */
    stats->success_count++;
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

    /* Clear congestion flag and retry */
    stats->congestion_flag = 0;

    /* Wait before retry */
    delay_type = 0;
    TIME_$WAIT(&delay_type, &timeout, &local_status);

    goto transmit_retry;

transmit_done:
    /*
     * Finalize status and return.
     */
    if (success >= 0) {
        success = ~success;
    }

    /* Store final state */
    stats->last_success = success;

    if (success < 0) {
        result_bytes[0] |= 0x02;
        stats->biphase_flag = 0;
    }

    /* Store send flag byte */
    stats->biphase_flag = ((uint8_t *)send_flags)[1];
}
