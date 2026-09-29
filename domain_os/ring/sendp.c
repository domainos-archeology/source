/*
 * RING_$SENDP - Token ring packet transmission
 *
 * Original address: 0x00E75916, 1226 bytes.
 *
 * Traced block by block from the listing.  The overall shape is:
 *
 *   0xE75916  prologue; unit_data = RING_$CTL.units[unit] (stride 0x244),
 *             stats = RING_$DATA[unit] (stride 0x3C, formed as
 *             unit*64 - unit*4), *result_flags = 0
 *   0xE7595A  three refusals: unit not initialised, data too long, the
 *             controller's mode register not ready
 *   0xE759AA  retry budget, the two "no retry" inputs, the header checksum
 *   0xE75A22  transmit_retry: arm the DMA and start the transmitter
 *   0xE75A4A  normal arm: 0x6000, then spin until RING_$CTL.poll_timeout
 *             has elapsed, then two absolute-deadline TIME_$WAIT2 calls
 *   0xE75B24  force-start arm: 0x7000 plus one relative TIME_$WAIT2
 *   0xE75B72  abort with 0x4000; a second relative wait if still busy
 *   0xE75C18  process_status: read the transmit CSR and decode it
 *   0xE75D90  retry gate, then TIME_$WAIT and back to transmit_retry
 *   0xE75DC2  transmit_done: publish last_success / biphase_flag
 *
 * Frame (link.w A6,-0x48):
 *   -0x10  the 16-byte data descriptor, copied from the caller's; its first
 *          longword is the DMA address handed to ring_$setup_tx_dma
 *   -0x18  in the poll loop the current time; from 0xE75C52 on, the retry
 *          delay.  The two are never live at the same time - 0xE75C52
 *          "clr.l (-0x18,A6) / move.w #0x1f4,(-0x14,A6)" rewrites all six
 *          bytes - so they are separate locals here.
 *   -0x20  the poll/wait deadline
 *   -0x28  the time the current attempt started
 *   -0x2c  the status TIME_$WAIT2 / TIME_$WAIT return, never examined
 *   -0x30  the eventcount value being waited for
 *   -0x38  the data length actually programmed
 *   -0x44  unit_data
 *
 * Register roles:
 *   D2  "no retry": hdr->flags bit 7 OR send_opts bit 0 (0xE759BC-0xE759CC)
 *   D4  force_start, set by the force-start arm (0xE75B44 "st D4b")
 *   D5  network activity, set by the two "st D5b" sites; 0xE75DC2 stores its
 *       ONE'S COMPLEMENT into stats->last_success
 *   D6  the retry budget
 *   D7  the eventcount target, then (low word only) the transmit CSR
 *
 * A note on the result word: every flag store is a byte operation on the word
 * at (A3).  "bset.b #n,(A3)" addresses the EVEN byte, which on m68k is the
 * high half, so it is bit n+8 of the word; "bset.b #n,(0x1,A3)" is bit n.
 * The masks below are written as word masks for that reason (bead source-x1es).
 */

#include "ring/ring_internal.h"
#include "misc/crash_system.h"
#include "time/time.h"
#include "mmu/mmu.h"
#include "parity/parity.h"
#include "cal/cal.h"   /* ADD48 / SUB48 */

/*
 * Transmit CSR bits, as tested by the status decode at 0xE75C18.
 */
#define RING_XMIT_CSR_COMPLETE      0x0014  /* the whole word, 0xE75C38 */
#define RING_XMIT_CSR_MODEM         0x0C00  /* 0xE75C5E andi.w #0xc00     */
#define RING_XMIT_CSR_BIPHASE       0x0800  /* 0xE75C64 btst #11          */
#define RING_XMIT_CSR_ESB           0x0400  /* 0xE75C76 btst #10          */
#define RING_XMIT_CSR_BUS           0x0040  /* 0xE75C8C btst #6           */
#define RING_XMIT_CSR_ORUN          0x0001  /* 0xE75CD2 btst #0           */
#define RING_XMIT_CSR_NORTN         0x0220  /* 0xE75CE6 andi.w #0x220     */
#define RING_XMIT_CSR_APAR          0x0080  /* 0xE75CF8 tst.b D7b         */
#define RING_XMIT_CSR_PKTERR        0x0018  /* 0xE75D12, both bits set    */
#define RING_XMIT_CSR_ACCEPTED      0x0004  /* 0xE75D28 btst #2           */
#define RING_XMIT_CSR_WACK          0x0002  /* 0xE75D34 btst #1           */
#define RING_XMIT_CSR_NOTOKEN       0x0010  /* 0xE75D4C btst #4           */
#define RING_XMIT_CSR_UNEXPECTED    0x0008  /* 0xE75D80 btst #3           */

/* The transmitter's mode/ready register (hw_regs+0x06). */
#define RING_MODE_READY             0x2000  /* 0xE7599A btst #13          */
#define RING_MODE_ALT               0x4000  /* 0xE75BFC btst #14          */

/* Commands written to the transmit CSR. */
#define RING_XMIT_CMD_START         0x6000  /* 0xE75A4A */
#define RING_XMIT_CMD_FORCE_START   0x7000  /* 0xE75B40 */
#define RING_XMIT_CMD_ABORT         0x4000  /* 0xE75B72 */

/* Values written to the mode register when congestion is first seen. */
#define RING_MODE_CONGESTED_ALT     0x6800  /* 0xE75C02 */
#define RING_MODE_CONGESTED         0x2800  /* 0xE75C0A */

/*
 * Result-word bits, as word masks (see the note above the includes).
 */
#define RING_SENDP_RES_NOT_READY    0x0010  /* bset.b #4,(0x1,A3) */
#define RING_SENDP_RES_ESB          0x0004  /* bset.b #2,(0x1,A3) */
#define RING_SENDP_RES_BIPHASE      0x0020  /* bset.b #5,(0x1,A3) */
#define RING_SENDP_RES_NO_RETURN    0x0040  /* bset.b #6,(0x1,A3) */
#define RING_SENDP_RES_BUS_ERR      0x0080  /* bset.b #7,(0x1,A3) */
#define RING_SENDP_RES_OVERRUN      0x0100  /* bset.b #0,(A3)     */
#define RING_SENDP_RES_RETRIED      0x0200  /* bset.b #1,(A3)     */
#define RING_SENDP_RES_DELAYED      0x0400  /* bset.b #2,(A3)     */
#define RING_SENDP_RES_CONGESTED    0x0800  /* bset.b #3,(A3)     */
#define RING_SENDP_RES_NACK         0x2000  /* bset.b #5,(A3)     */
#define RING_SENDP_RES_WACK         0x4000  /* bset.b #6,(A3)     */
#define RING_SENDP_RES_OK           0x8000  /* bset.b #7,(A3)     */

/*
 * The two Pascal by-reference delay-type constants that live behind the rts:
 *
 *   gsk read 0x00E75DDE
 *   00e75dde  4e 75 00 01 00 00 ...
 *             ^rts  ^0xE75DE0
 *                         ^0xE75DE2
 *
 * 0xE75ADC "pea (0x302,PC)" and 0xE75B1E "pea (0x2c0,PC)" both reach
 * 0x00E75DE0 (= 1, an ABSOLUTE deadline), while 0xE75B5E "pea (0x282,PC)",
 * 0xE75B96 "pea (0x24a,PC)" and 0xE75DB0 "pea (0x30,PC)" reach 0x00E75DE2
 * (= 0, a RELATIVE delay).
 */
static const uint16_t ring_$c_delay_absolute = 1;   /* 0x00E75DE0 */
static const uint16_t ring_$c_delay_relative = 0;   /* 0x00E75DE2 */

/*
 * Read and write the bottom 32 bits of the 48-bit value at A6-0x18.
 *
 * 0xE75D42 "move.l #0x1f4,(-0x16,A6)" and 0xE75D76 "move.l #0xfa,(-0x16,A6)"
 * write a LONGWORD two bytes into the clock, i.e. across the low half of its
 * high longword and all of its low word.  Going through the fields keeps that
 * correct on a little-endian host.
 */
static void ring_$set_delay_low32(clock_t *c, uint32_t value)
{
    c->high = (c->high & 0xFFFF0000u) | (value >> 16);
    c->low = (uint16_t)value;
}

/*
 * Read the bottom 32 bits of the 48-bit value at A6-0x28, which 0xE75AFE
 * "and.l (-0x26,A6),D0" uses as the source of the backoff jitter.
 */
static uint32_t ring_$get_clock_low32(const clock_t *c)
{
    return ((c->high & 0x0000FFFFu) << 16) | (uint32_t)c->low;
}

/*
 * RING_$SENDP - Send a packet on the token ring
 *
 * @param unit_ptr      Pointer to the unit number word            (0x08)
 * @param hdr_pa        Header DMA address                         (0x0C)
 * @param hdr           The header being sent; its msg_type,
 *                      flags and chksum fields are all used       (0x10)
 * @param hdr_len       Header byte count, passed BY ADDRESS to
 *                      HDR_CHKSUM ("pea (0x14,A6)" at 0xE75A00)   (0x14)
 * @param data_desc     Four longwords describing the payload; only
 *                      the first is used after the copy           (0x16)
 * @param unused_1a     A longword parameter the body never reads  (0x1A)
 * @param data_len      Payload byte count, 0 for a header-only
 *                      packet                                     (0x1E)
 * @param send_opts     A word whose bit 0 forbids retrying
 *                      (0xE759C6 "btst.b D4,(0x1,A1)", D4 == 0)   (0x20)
 * @param result_flags  Output: the result word                    (0x24)
 * @param status_ret    Output: status code                        (0x28)
 */
void RING_$SENDP(uint16_t *unit_ptr, uint32_t hdr_pa, ring_$pkt_hdr_t *hdr,
                 uint16_t hdr_len, const uint32_t *data_desc,
                 uint32_t unused_1a, uint16_t data_len,
                 const uint16_t *send_opts, uint16_t *result_flags,
                 status_$t *status_ret)
{
    uint16_t unit;
    ring_unit_t *unit_data;             /* A6-0x44 */
    ring_$stats_t *stats;               /* A2 */
    ring_hw_regs_t *hw;                 /* A4 */

    int32_t tx_ec_target;               /* D7, before the CSR read */
    uint16_t hw_status;                 /* D7 low word, after 0xE75C26 */
    int16_t retry_budget;               /* D6 */
    int8_t force_start;                 /* D4 */
    int8_t activity;                    /* D5 */
    int8_t no_retry;                    /* D2 */

    clock_t attempt_start;              /* A6-0x28 */
    clock_t deadline;                   /* A6-0x20 */
    clock_t poll_now;                   /* A6-0x18, phase 1 */
    clock_t retry_delay;                /* A6-0x18, phase 2 */
    status_$t wait_status;              /* A6-0x2C */
    uint32_t ec_target_cell;            /* A6-0x30 */
    uint16_t dma_data_len;              /* A6-0x38 */
    uint32_t data_buf[4];               /* A6-0x10 */

    const uint16_t *wait_delay_type;
    clock_t *wait_delay;

    (void)unused_1a;

    /* 0xE75930..0xE75954 */
    unit = *unit_ptr;
    unit_data = &RING_$CTL.units[unit];
    stats = &RING_$WIRED_DATA.stats[unit];

    /* 0xE75958 */
    *result_flags = 0;

    /*
     * 0xE7595A "tst.b (0x60,A1) / bmi": the unit must be initialised.  This
     * arm branches to the epilogue at 0xE75DD6, skipping the transmit_done
     * stores entirely.
     */
    if (unit_data->initialized >= 0) {
        *result_flags |= RING_SENDP_RES_NOT_READY;      /* 0xE75960 */
        *status_ret = status_$io_controller_not_in_system;
        return;
    }

    /* 0xE75974 "cmp.w (0x51a,A5),D0w / bls" */
    if (data_len > RING_$CTL.driver.max_data_len) {
        *status_ret = status_$network_data_length_too_large;
        return;
    }

    /* 0xE75988: the default outcome is "transmit failed" */
    *status_ret = status_$network_transmit_failed;

    /* 0xE75992 */
    hw = unit_data->hw_regs;

    /* 0xE75996 "move.w (0x6,A4),D2w / btst.l #0xd,D2 / bne" */
    if ((hw->mode & RING_MODE_READY) == 0) {
        *result_flags |= RING_SENDP_RES_NOT_READY;      /* 0xE759A0 */
        return;
    }

    /*
     * 0xE759AA: twenty attempts normally, ten if the PREVIOUS send ended
     * without a hard error ("tst.b (0x34,A2) / bpl" skips the moveq #0xa).
     */
    retry_budget = 0x14;
    if (stats->last_success < 0) {
        retry_budget = 0x0A;
    }

    /* 0xE759B4 */
    force_start = 0;
    activity = 0;

    /*
     * 0xE759BC-0xE759CC: either the header's own flags bit 7 or bit 0 of the
     * caller's option word suppresses the retry that would otherwise follow
     * the "delayed"/"no response"/"packet error" arms.  The btst uses D4 as
     * the bit number and D4 is still zero here.
     */
    no_retry = ((int8_t)hdr->flags < 0) ? (int8_t)-1 : 0;
    if ((*send_opts & 0x0001) != 0) {
        no_retry = (int8_t)-1;
    }

    /* 0xE759CE / 0xE759D2 */
    stats->xmit_call++;
    stats->retry_pending = 0;

    /* 0xE759D6 */
    if (data_len == 0) {
        dma_data_len = 0;
        data_buf[0] = hdr_pa;                           /* 0xE759DE */
    } else {
        dma_data_len = data_len;
        /* 0xE759E8-0xE759F6: four "move.l (A0)+,(A1)+" */
        data_buf[0] = data_desc[0];
        data_buf[1] = data_desc[1];
        data_buf[2] = data_desc[2];
        data_buf[3] = data_desc[3];
    }

    /*
     * 0xE759F8: NETWORK_$DO_CHKSUM (0x00E24C46).  HDR_CHKSUM is handed the
     * ADDRESS of the hdr_len parameter slot ("pea (0x14,A6)"), which is
     * exactly &hdr_len here.
     */
    if (NETWORK_$DO_CHKSUM < 0) {
        hdr->chksum = HDR_CHKSUM(hdr, &hdr_len);        /* 0xE75A12 */
    } else {
        hdr->chksum = 1;                                /* 0xE75A1C */
    }

transmit_retry:
    /* 0xE75A22 */
    tx_ec_target = (int32_t)unit_data->tx_ec.value + 1;

    /* 0xE75A2C-0xE75A3A */
    ring_$setup_tx_dma(hdr_pa, (int16_t)hdr_len, data_buf[0],
                       (int16_t)dma_data_len);

    /* 0xE75A42 "tst.b (0x3a,A2) / bmi.w 0xE75B24" */
    if (stats->retry_pending < 0) {
        /*
         * Force-start arm, 0xE75B24.  TIME_$ABS_CLOCK writes the GLOBAL
         * RING_$CTL.force_start_timeout and SUB48 then subtracts the time
         * this attempt started, so the global ends up holding how long the
         * attempt has been running.
         */
        TIME_$ABS_CLOCK(&RING_$CTL.force_start_timeout);
        SUB48(&RING_$CTL.force_start_timeout, &attempt_start);

        hw->xmit_csr = RING_XMIT_CMD_FORCE_START;       /* 0xE75B40 */
        force_start = (int8_t)-1;                       /* 0xE75B44 "st D4b" */

        wait_delay_type = &ring_$c_delay_relative;      /* 0xE75B5E */
        wait_delay = &RING_$CTL.xmit_timeout1;         /* 0xE75B5A */
        goto shared_wait;
    }

    /* 0xE75A4A: normal arm */
    hw->xmit_csr = RING_XMIT_CMD_START;

    /* 0xE75A4E */
    TIME_$ABS_CLOCK(&attempt_start);

    /* 0xE75A5A: deadline = attempt_start + RING_$CTL.poll_timeout */
    deadline = attempt_start;
    ADD48(&deadline, &RING_$CTL.poll_timeout);

    /*
     * 0xE75A78-0xE75AA2: spin until either the transmit eventcount reaches
     * the target or the deadline has passed.  SUB48 ends in "spl", so
     * "bpl" (D0 == 0) means the difference went negative, i.e. the deadline
     * is still ahead.
     */
    for (;;) {
        if (tx_ec_target <= (int32_t)unit_data->tx_ec.value) {
            goto process_status;                        /* 0xE75A80 */
        }
        TIME_$ABS_CLOCK(&poll_now);                     /* 0xE75A84 */
        if (SUB48(&poll_now, &deadline) < 0) {          /* 0xE75A98 */
            break;
        }
    }

    /* 0xE75AA4: deadline = attempt_start + RING_$CTL.wait_timeout */
    deadline = attempt_start;
    ADD48(&deadline, &RING_$CTL.wait_timeout);

    RING_$CTL.xmit_waited++;                                /* 0xE75AC0 */

    wait_delay_type = &ring_$c_delay_absolute;          /* 0xE75ADC */
    wait_delay = &deadline;
    ec_target_cell = (uint32_t)tx_ec_target;            /* 0xE75AC4 */
    if (TIME_$WAIT2((uint16_t *)wait_delay_type, wait_delay,
                    &unit_data->tx_ec, &ec_target_cell, &wait_status) < 0) {
        goto process_status;                            /* 0xE75AEC */
    }

    /*
     * 0xE75AF0-0xE75B22: a second absolute wait, with 0..7 added to the
     * HIGH longword of the deadline - i.e. a jitter of up to seven times
     * 65536 ticks - taken from the bottom 32 bits of attempt_start.
     */
    deadline = attempt_start;
    deadline.high += (ring_$get_clock_low32(&attempt_start) & 7u);

    wait_delay_type = &ring_$c_delay_absolute;          /* 0xE75B1E */
    wait_delay = &deadline;

shared_wait:
    /* 0xE75B46 / 0xE75B62: the one call all three setups fall into */
    ec_target_cell = (uint32_t)tx_ec_target;
    if (TIME_$WAIT2((uint16_t *)wait_delay_type, wait_delay,
                    &unit_data->tx_ec, &ec_target_cell, &wait_status) < 0) {
        goto process_status;                            /* 0xE75B6E */
    }

    /* 0xE75B72: ask the transmitter to give up */
    hw->xmit_csr = RING_XMIT_CMD_ABORT;

    /* 0xE75B76 */
    if ((hw->xmit_csr & RING_MODE_READY) != 0) {
        /* 0xE75B7E: still running - one more relative wait */
        wait_delay_type = &ring_$c_delay_relative;      /* 0xE75B96 */
        wait_delay = &RING_$CTL.xmit_timeout2;         /* 0xE75B92 */
        ec_target_cell = (uint32_t)tx_ec_target;        /* 0xE75B7E */
        (void)TIME_$WAIT2((uint16_t *)wait_delay_type, wait_delay,
                          &unit_data->tx_ec, &ec_target_cell, &wait_status);

        /* 0xE75BA4 */
        if ((hw->xmit_csr & RING_MODE_READY) != 0) {
            goto hardware_error;                        /* 0xE75BAA */
        }
    }

    /* 0xE75BAC */
    if (tx_ec_target <= (int32_t)unit_data->tx_ec.value) {
        goto process_status;
    }

    /* 0xE75BB6 */
    ring_$clear_dma_channel(2, unit);

    /* 0xE75BC6: a retry was already pending, so the controller is stuck */
    if (stats->retry_pending < 0) {
hardware_error:
        *status_ret = status_$ring_controller_hardware_error;  /* 0xE75BCC */
        goto transmit_done;
    }

    /* 0xE75BDA: the force start has already been tried once */
    if (force_start < 0) {
        goto transmit_done;
    }

    stats->xmit_tim++;                                  /* 0xE75BE0 */

    /*
     * 0xE75BE4-0xE75BEE: "move.b (0x36,A2),D0b / not.b D0b /
     * and.b (0x38,A2),D0b / bpl" - enter congested mode the first time the
     * biphase flag is set while the congestion flag is still clear.
     */
    if ((int8_t)((uint8_t)~(uint8_t)stats->congestion_flag &
                 (uint8_t)stats->biphase_flag) < 0) {
        stats->congestion_flag = (int8_t)-1;            /* 0xE75BF0 */
        *result_flags |= RING_SENDP_RES_CONGESTED;      /* 0xE75BF4 */

        /* 0xE75BF8 */
        if ((hw->mode & RING_MODE_ALT) != 0) {
            hw->mode = RING_MODE_CONGESTED_ALT;         /* 0xE75C02 */
        } else {
            hw->mode = RING_MODE_CONGESTED;             /* 0xE75C0A */
        }
    }

    /* 0xE75C10: this arm always retries; it never decodes the status */
    stats->retry_pending = (int8_t)-1;
    goto transmit_retry;

process_status:
    /* 0xE75C18: a Pascal function whose result the caller discards */
    MMU_$MCR_CHANGE(4);

    hw_status = hw->xmit_csr;                           /* 0xE75C26 */

    ring_$clear_dma_channel(2, unit);                   /* 0xE75C28 */

    /* 0xE75C38: the whole word, not a mask */
    if (hw_status == RING_XMIT_CSR_COMPLETE) {
        *result_flags |= RING_SENDP_RES_OK;             /* 0xE75C3E */
        stats->xmitcnt++;                               /* 0xE75C42 */
        activity = (int8_t)-1;                          /* 0xE75C46 */
        NETWORK_$ACTIVITY_FLAG = activity;              /* 0xE75C48 */
        goto report_ok;                                 /* 0xE75C4E */
    }

    /* 0xE75C52: the default retry delay, 500 ticks */
    retry_delay.high = 0;
    retry_delay.low = 0x01F4;

    /* 0xE75C5C */
    if ((hw_status & RING_XMIT_CSR_MODEM) != 0) {
        if ((hw_status & RING_XMIT_CSR_BIPHASE) != 0) {
            *result_flags |= RING_SENDP_RES_BIPHASE;    /* 0xE75C6A */
            RING_$WIRED_DATA.xmit_biphase++;                       /* 0xE75C70 */
        }
        if ((hw_status & RING_XMIT_CSR_ESB) != 0) {
            *result_flags |= RING_SENDP_RES_ESB;        /* 0xE75C7C */
            RING_$WIRED_DATA.xmit_esb++;                           /* 0xE75C82 */
        }
        stats->xmit_modem++;                            /* 0xE75C88 */
    }

    /* 0xE75C8C */
    if ((hw_status & RING_XMIT_CSR_BUS) != 0) {
        /*
         * 0xE75C92-0xE75CB0: both DMA addresses are turned into page frame
         * numbers ("lsr.l #0x8" then "lsr.l #0x2") and offered to
         * PARITY_$CHK_IO; a result of 2 means the parity error was ours.
         */
        if (PARITY_$CHK_IO(hdr_pa >> 10, data_buf[0] >> 10) == 2) {
            *status_ret = status_$network_memory_parity_error_during_transmit;
            goto transmit_done;                         /* 0xE75CC0 */
        }
        *result_flags |= RING_SENDP_RES_BUS_ERR;        /* 0xE75CC4 */
        stats->xmit_bus++;                              /* 0xE75CCA */
        goto retry;                                     /* 0xE75CCE */
    }

    /* 0xE75CD2 */
    if ((hw_status & RING_XMIT_CSR_ORUN) != 0) {
        *result_flags |= RING_SENDP_RES_OVERRUN;        /* 0xE75CD8 */
        stats->xmit_orun++;                             /* 0xE75CDC */
        goto retry;                                     /* 0xE75CE0 */
    }

    /* 0xE75CE4 */
    if ((hw_status & RING_XMIT_CSR_NORTN) != 0) {
        *result_flags |= RING_SENDP_RES_DELAYED;        /* 0xE75CEC */
        stats->xmit_nortn++;                            /* 0xE75CF0 */
        goto retry_if_allowed;                          /* 0xE75CF4 */
    }

    /* 0xE75CF8 "tst.b D7b / bpl": bit 7 of the CSR's low byte */
    if ((hw_status & RING_XMIT_CSR_APAR) != 0) {
        *result_flags |= RING_SENDP_RES_NO_RETURN;      /* 0xE75CFC */
        stats->xmit_apar++;                             /* 0xE75D02 */
        goto retry_if_allowed;                          /* 0xE75D06 */
    }

    /* 0xE75D0A: everything from here on counts as network activity */
    activity = (int8_t)-1;
    NETWORK_$ACTIVITY_FLAG = activity;

    /* 0xE75D12: both bits of 0x18 set */
    if ((hw_status & RING_XMIT_CSR_PKTERR) == RING_XMIT_CSR_PKTERR) {
        *result_flags |= RING_SENDP_RES_NO_RETURN;      /* 0xE75D1C */
        stats->xmit_error++;                            /* 0xE75D22 */
        goto retry_if_allowed;                          /* 0xE75D26 */
    }

    /* 0xE75D28 */
    if ((hw_status & RING_XMIT_CSR_ACCEPTED) != 0) {
        *result_flags |= RING_SENDP_RES_OK;             /* 0xE75D2E */
        goto count_and_report_ok;                       /* 0xE75D32 */
    }

    /* 0xE75D34 */
    if ((hw_status & RING_XMIT_CSR_WACK) != 0) {
        *result_flags |= RING_SENDP_RES_WACK;           /* 0xE75D3A */
        stats->xmit_wack++;                             /* 0xE75D3E */
        ring_$set_delay_low32(&retry_delay, 0x1F4);     /* 0xE75D42 */
        goto retry;                                     /* 0xE75D4A */
    }

    /* 0xE75D4C */
    if ((hw_status & RING_XMIT_CSR_NOTOKEN) == 0) {
        *result_flags |= RING_SENDP_RES_NACK;           /* 0xE75D52 */

        /*
         * 0xE75D56-0xE75D64: a known message type (1..4) is treated as
         * delivered; anything else - including 0 - is a "no acknowledge".
         */
        if (hdr->msg_type != 0 && hdr->msg_type <= 4) {
count_and_report_ok:
            stats->xmitcnt++;                           /* 0xE75D66 */
report_ok:
            *status_ret = status_$ok;                   /* 0xE75D6E */
            goto transmit_done;
        }

        stats->xmit_nack++;                             /* 0xE75D72 */
        ring_$set_delay_low32(&retry_delay, 0xFA);      /* 0xE75D76 */
        goto retry;                                     /* 0xE75D7E */
    }

    /* 0xE75D80 */
    if ((hw_status & RING_XMIT_CSR_UNEXPECTED) == 0) {
        RING_$CTL.unexpected_xmit_stat = hw_status;         /* 0xE75D86 */
    }
    *result_flags |= RING_SENDP_RES_NO_RETURN;          /* 0xE75D8A */

retry_if_allowed:
    /* 0xE75D90 */
    if (no_retry < 0) {
        goto transmit_done;
    }

retry:
    /* 0xE75D94 */
    retry_budget--;
    if (retry_budget == 0) {
        goto transmit_done;
    }

    /* 0xE75D98 "btst.b #0x1,(0x4,A0)" - the header's own no-retry bit */
    if ((hdr->flags & 0x02) != 0) {
        goto transmit_done;
    }

    stats->retry_pending = 0;                           /* 0xE75DA4 */

    /* 0xE75DA8-0xE75DB4 */
    TIME_$WAIT((uint16_t *)&ring_$c_delay_relative, &retry_delay,
               &wait_status);

    goto transmit_retry;                                /* 0xE75DBE */

transmit_done:
    /*
     * 0xE75DC2 "not.b D5b" is UNCONDITIONAL and 0xE75DC4 sets the condition
     * codes the following "bpl" tests, so last_success ends up true exactly
     * when neither "st D5b" fired.
     */
    activity = (int8_t)~(uint8_t)activity;
    stats->last_success = activity;

    if (activity < 0) {
        *result_flags |= RING_SENDP_RES_RETRIED;        /* 0xE75DCA */
        /*
         * 0xE75DCE "clr.b (0x38,A2)" is dead in the original - the very next
         * instruction stores D4 over it on both paths.  Kept as written.
         */
        stats->biphase_flag = 0;
    }

    /* 0xE75DD2: the force-start flag, not a byte of any argument */
    stats->biphase_flag = force_start;
}
