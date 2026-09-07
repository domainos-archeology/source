/*
 * Ring module receive functions
 *
 * RING_$RCV0 / RING_$RCV1 are the per-unit receive daemon entry points; both
 * are one-line wrappers around RING_$RCV_FROM_UNIT_PRIV, which is the receive
 * loop proper.
 *
 * ring_$validate_receive (0x00E75DE4) is a *nested* Pascal procedure of
 * RING_$RCV_FROM_UNIT_PRIV: it takes no arguments and reaches into its
 * parent's stack frame through the static link ("movea.l (A6),A2" at
 * 0x00E75DEC), reading and writing the locals at A6-0x18 .. A6-0x30 and the
 * unit parameter at A6+0x08.  Per the project conventions it is flattened
 * here into a static function that takes the shared frame by reference.
 */

#include "ring/ring_internal.h"
#include "misc/crash_system.h"
#include "proc1/proc1.h"
/* IO_$GET_DCTE: io/io.h (via ring/ring_internal.h) */

/*
 * ============================================================================
 * PC-relative constant cells
 *
 * Apollo Pascal passes literal actual parameters of var/const formals by
 * reference, placing the literal in the code region and pushing "pea (d,PC)".
 * Each of these is one of those cells.
 * ============================================================================
 */

/*
 * 0x00E75DE2: the delay-type word handed to TIME_$WAIT at 0x00E761B0
 * ("pea (-0x3d0,PC)").  Zero == relative delay.
 */
static const uint16_t ring_$rcv_delay_type = 0x0000;

/*
 * 0x00E7628C: the status handed to CRASH_SYSTEM at 0x00E761D8
 * ("pea (0xb2,PC)") when the receiver is still busy after the reset and the
 * delay.  0x00110005 == status_$network_receive_process_failed_to_start.
 */
static const status_$t ring_$rcv_stuck_status = 0x00110005;

/*
 * 0x00E76040: the status handed to CRASH_SYSTEM at 0x00E75ED8
 * ("pea (0x166,PC)") on a header checksum mismatch.
 */
static const status_$t ring_$rcv_chksum_status = 0x00110010;

/*
 * 0x00E76044: the status handed to CRASH_SYSTEM at 0x00E75FB4
 * ("pea (0x8e,PC)") when rcv_csr bit 6 is set.
 */
static const status_$t ring_$rcv_stat40_status = 0x00110013;

/*
 * ============================================================================
 * The stack frame shared between RING_$RCV_FROM_UNIT_PRIV and its nested
 * procedure ring_$validate_receive.
 *
 * The comment on each field gives the A6 displacement in the original frame
 * ("link.w A6,-0x3c" at 0x00E76048).
 * ============================================================================
 */
typedef struct ring_rcv_frame_t {
    uint16_t            unit;           /* A6+0x08: the routine's parameter   */
    status_$t           status;         /* A6-0x08                            */
    ring_hw_regs_t     *hw_regs;        /* A6-0x0C                            */
    clock_t             delay;          /* A6-0x14 (high) / A6-0x10 (low)     */
    ring_$pkt_hdr_t    *hdr;            /* A6-0x18: received header (VA)      */
    uint32_t            data_pa;        /* A6-0x1C: data buffer, 0 if unused  */
    int16_t             rcv_data_len;   /* A6-0x24: bytes DMAd into the data  */
    int16_t             rcv_hdr_len;    /* A6-0x26: bytes DMAd into the hdr   */
    int16_t             hdr_chksum;     /* A6-0x28: hdr->chksum, zero extended */
    int16_t             hdr_hdr_len;    /* A6-0x2A: hdr->hdr_len              */
    int16_t             hdr_data_len;   /* A6-0x2C: hdr->data_len             */
    int16_t             hdr_pkt_class;  /* A6-0x2E: hdr->pkt_class, zero ext. */
    uint16_t            rcv_status;     /* A6-0x30: snapshot of hw rcv_csr    */
} ring_rcv_frame_t;

static boolean ring_$validate_receive(ring_rcv_frame_t *fr);

/*
 * RING_$RCV0 - Receive daemon for unit 0
 *
 * Original address: 0x00E76642
 *
 * Assembly:
 *   link.w A6,0x0
 *   pea (A5)
 *   lea (0xe86400).l,A5        ; Load RING_$DATA base
 *   subq.l #0x2,SP
 *   clr.w -(SP)                 ; Push unit 0
 *   bsr.w RING_$RCV_FROM_UNIT_PRIV
 *   ... (never returns)
 */
void RING_$RCV0(void)
{
    RING_$RCV_FROM_UNIT_PRIV(0);
    /* Never returns */
}

/*
 * RING_$RCV1 - Receive daemon for unit 1
 *
 * Original address: 0x00E7665E
 */
void RING_$RCV1(void)
{
    RING_$RCV_FROM_UNIT_PRIV(1);
    /* Never returns */
}

/*
 * RING_$RCV_FROM_UNIT_PRIV - Privileged receive loop
 *
 * Runs forever.  Each pass:
 *   1. tops up the header and data buffers,
 *   2. programs the receive DMA and arms the controller,
 *   3. blocks on the unit's receive eventcount,
 *   4. either recovers from a stuck receiver or validates and dispatches the
 *      packet that arrived.
 *
 * Original address: 0x00E76048
 *
 * @param unit          Unit number (0 or 1)
 */
void RING_$RCV_FROM_UNIT_PRIV(uint16_t unit)
{
    ring_rcv_frame_t fr;
    ring_unit_t *unit_data;
    ring_$stats_t *stats;
    boolean first_time;             /* D2: 0xFF until ready_ec is advanced */
    int32_t wait_val;               /* D3 */
    boolean valid;

    fr.unit = unit;

    /* 0x00E76056: st D2b */
    first_time = true;

    /*
     * 0x00E76058-0x00E76060: A3 = &RING_$DATA.units[unit].  Note this is
     * computed from the raw parameter *before* IO_$GET_DCTE validates it.
     */
    unit_data = &RING_$DATA.units[unit];

    /*
     * 0x00E76064-0x00E76076: IO_$GET_DCTE(&ring_dcte_ctype_net, &unit,
     * &status).  The controller type is the constant cell at 0x00E7628A,
     * passed by reference ("pea (0x21c,PC)").
     */
    IO_$GET_DCTE(&ring_dcte_ctype_net, &fr.unit, &fr.status);
    if (fr.status != status_$ok) {
        CRASH_SYSTEM(&fr.status);
    }

    /* 0x00E7608C: PROC1_$SET_LOCK(0x0D) - the ring receive resource lock. */
    PROC1_$SET_LOCK(0x0D);

    /* 0x00E7609A: cache the controller registers (unit + 0x1C). */
    fr.hw_regs = unit_data->hw_regs;

    /* 0x00E760A0/0x00E760AA: wait_val = rx_wake_ec.value + 1 */
    wait_val = unit_data->rx_wake_ec.value + 1;

    /* 0x00E760AC: D5 = RING_$STATS base; the unit is 0 based. */
    stats = &RING_$STATS[unit];

    for (;;) {                                      /* 0x00E760B8 */
        /*
         * 0x00E760BA: top up the data buffer if the previous packet consumed
         * it.
         */
        if (unit_data->rx_data_pa == 0) {
            NETBUF_$GET_DAT(&unit_data->rx_data_pa);
        }

        /*
         * 0x00E760CC: and the header buffer.  The arguments are pushed
         * right to left at 0x00E760D2/0x00E760D6, so &rx_hdr_pa is the first
         * argument and &rx_hdr the second.
         */
        if (unit_data->rx_hdr_pa == 0) {
            NETBUF_$GET_HDR(&unit_data->rx_hdr_pa,
                            (uint32_t *)&unit_data->rx_hdr);
        }

        /* 0x00E760E2: ring_$setup_rx_dma(hdr_pa, data_pa) */
        ring_$setup_rx_dma(unit_data->rx_hdr_pa, unit_data->rx_data_pa);

        /*
         * 0x00E760F4: "move.b (0x33,A3),(0x4,A0)" - the LOW byte of the
         * transmit mask word at unit+0x32 goes into the controller's byte
         * register at +4.
         */
        fr.hw_regs->tmask = (uint8_t)(unit_data->tmask & 0x00FF);

        /* 0x00E760FA: bclr.b #2,(0x31,A3) */
        unit_data->state_flags &= (uint8_t)~RING_UNIT_BUSY;

        if (unit_data->tmask == 0) {                /* 0x00E76100 */
            /* 0x00E76134: receiver idle */
            fr.hw_regs->mode = RING_MODE_IDLE;
        } else {
            /* 0x00E7610A/0x00E76114: arm the receiver */
            RING_$RCV_CSR_WRITE(fr.hw_regs, RING_RCV_CSR_ARM);
            fr.hw_regs->mode = RING_MODE_ENABLE;
            /* 0x00E7612A: clr.b (0x36,A0,D0w) */
            stats->congestion_flag = 0;
        }

        /*
         * 0x00E7613A: the first time round, tell whoever started the unit
         * that the daemon is up.
         */
        if (first_time < 0) {
            EC_$ADVANCE(&unit_data->ready_ec);
            first_time = false;
        }

        /*
         * 0x00E7614C-0x00E76160: ec_$wait with two three-element arrays
         * passed by value - 24 bytes in all, popped by the caller with
         * "lea (0x18,SP),SP".  No result slot is reserved, so the index the
         * function returns in D0 is discarded.
         */
        (void)EC_$WAIT((ec_$wait_ecs_t){{ &unit_data->rx_wake_ec, NULL, NULL }},
                       (ec_$wait_vals_t){{ wait_val, 0, 0 }});

        wait_val++;                                 /* 0x00E76164 */
        RING_$WAKEUP_CNT++;                         /* 0x00E76166 */

        /* 0x00E7616A: snapshot the header pointer for the nested procedure. */
        fr.hdr = unit_data->rx_hdr;

        /*
         * 0x00E76170: "btst.b #2,(0x31,A3)" / "beq.w 0x00E76202".  The BUSY
         * bit being SET takes the recovery path; the branch that is taken
         * when it is CLEAR only counts the condition.
         */
        if ((unit_data->state_flags & RING_UNIT_BUSY) != 0) {
            /* 0x00E7617A */
            if ((RING_$RCV_CSR_READ(fr.hw_regs) & RING_RCV_CSR_BUSY) != 0) {
                /* 0x00E7618A: try to shut the receiver down. */
                RING_$RCV_CSR_WRITE(fr.hw_regs, 0);

                if ((RING_$RCV_CSR_READ(fr.hw_regs) & RING_RCV_CSR_BUSY) != 0) {
                    /* 0x00E7619E: delay ~0xABE ticks and look again. */
                    fr.delay.high = 0;
                    fr.delay.low = 0xABE;
                    TIME_$WAIT((uint16_t *)&ring_$rcv_delay_type, &fr.delay,
                               &fr.status);
                    if (fr.status != status_$ok) {
                        /* 0x00E761C4 */
                        CRASH_SYSTEM(&fr.status);
                    } else if ((RING_$RCV_CSR_READ(fr.hw_regs) & RING_RCV_CSR_BUSY) != 0) {
                        /* 0x00E761D8 */
                        CRASH_SYSTEM(&ring_$rcv_stuck_status);
                    }
                }

                /* 0x00E761E4: drop both receive channels and start over. */
                ring_$clear_dma_channel(0, unit);
                ring_$clear_dma_channel(1, unit);
                continue;
            }
            /* rcv_csr idle: join the common tail at 0x00E76214. */
        } else {
            /* 0x00E76202 */
            if ((RING_$RCV_CSR_READ(fr.hw_regs) & RING_RCV_CSR_BUSY) != 0) {
                RING_$BUSY_ON_RCV_INT++;            /* 0x00E76210 */
            }
        }

        /* 0x00E76214/0x00E76220 */
        ring_$clear_dma_channel(0, unit);
        ring_$clear_dma_channel(1, unit);

        /* 0x00E7622E */
        valid = ring_$validate_receive(&fr);
        if (valid < 0) {
            /* 0x00E76236: st NETWORK_$ACTIVITY_FLAG */
            NETWORK_$ACTIVITY_FLAG = true;

            /*
             * 0x00E7623C: "tst.w (-0x2c,A6)" - hdr_data_len, which the
             * nested procedure filled in from hdr->data_len.
             */
            if (fr.hdr_data_len > 0) {
                fr.data_pa = unit_data->rx_data_pa;
            } else {
                fr.data_pa = 0;
            }

            /*
             * 0x00E7624E-0x00E76268: every record argument is passed by
             * address; the word result slot the caller reserves is discarded.
             */
            (void)ring_$receive_packet(unit, &fr.hdr, &fr.data_pa,
                                       &fr.rcv_hdr_len, &fr.rcv_data_len);

            /*
             * 0x00E7626C: data_pa is re-read AFTER the call - the callee may
             * have cleared it to say it did not keep the data buffer.
             */
            unit_data->rx_hdr_pa = 0;
            if (fr.data_pa != 0) {
                unit_data->rx_data_pa = 0;
            }
        } else {
            RING_$ABORT_CNT++;                      /* 0x00E76282 */
        }
    }
}

/*
 * ring_$validate_receive - decide whether the packet that just arrived may be
 * handed on, and account for it either way.
 *
 * Nested procedure of RING_$RCV_FROM_UNIT_PRIV; see the file header.  The
 * frame fields it reads (hdr, hw_regs, unit) and writes (rcv_status,
 * rcv_hdr_len, rcv_data_len, hdr_*) are exactly the parent locals the
 * original touches through the static link.
 *
 * Original address: 0x00E75DE4
 *
 * @return true (0xFF) if the packet is good, false otherwise.
 */
static boolean ring_$validate_receive(ring_rcv_frame_t *fr)
{
    boolean result;                 /* D2 */
    boolean is_swdiag;              /* D0 */
    ring_$stats_t *stats;           /* A3 */
    ring_$swdiag_t *swdiag;         /* A0 */
    ring_$pkt_hdr_t *hdr;           /* A4 */
    int16_t chksum;

    result = false;                                 /* 0x00E75DEE */

    /* 0x00E75DF0-0x00E75E04: A3 = &RING_$STATS[unit] */
    stats = &RING_$STATS[fr->unit];

    /* 0x00E75E08 */
    swdiag = &RING_$SWDIAG_DATA;

    /* 0x00E75E12 */
    fr->rcv_status = RING_$RCV_CSR_READ(fr->hw_regs);

    /*
     * 0x00E75E1C: "andi.w #-0x17,D1w" - every bit except 1, 2 and 4 is an
     * error condition.
     */
    if ((fr->rcv_status & 0xFFE9) != 0) {
        goto error_path;                            /* 0x00E75EF4 */
    }

    /*
     * 0x00E75E24-0x00E75E44: both receive channels are programmed for 0x400
     * words; the residual count says how much was transferred.
     */
    fr->rcv_hdr_len = (int16_t)(RING_DMA_RX_WORDS -
                                (*RING_DMA_CHAN0_COUNT) * 2);
    fr->rcv_data_len = (int16_t)(RING_DMA_RX_WORDS -
                                 (*RING_DMA_CHAN1_COUNT) * 2);

    /* 0x00E75E48-0x00E75E6A: pull the advertised lengths out of the header. */
    hdr = fr->hdr;
    fr->hdr_pkt_class = (int16_t)hdr->pkt_class;
    fr->hdr_chksum = (int16_t)hdr->chksum;
    fr->hdr_hdr_len = (int16_t)hdr->hdr_len;
    fr->hdr_data_len = (int16_t)hdr->data_len;

    /*
     * 0x00E75E6C-0x00E75E7C: the DMA always moves whole words, so an odd
     * advertised length transferred one byte too many.
     */
    fr->rcv_data_len -= (int16_t)(fr->hdr_data_len & 1);
    fr->rcv_hdr_len -= (int16_t)(fr->hdr_hdr_len & 1);

    /* 0x00E75E80 */
    if (fr->hdr_data_len == 0) {
        fr->rcv_data_len = 0;
    }

    /* 0x00E75E8A-0x00E75E9C */
    if (fr->rcv_data_len != fr->hdr_data_len ||
        fr->rcv_hdr_len != fr->hdr_hdr_len) {
        RING_$BAD_DATA_CNT++;                       /* 0x00E75E9E */
        goto done;
    }

    /*
     * 0x00E75EA6-0x00E75EB4: checksum the header unless the packet class is
     * 1, unless checksumming is globally off, or unless bit 0 of hdr->flags
     * says the sender did not compute one.
     */
    if ((boolean)((fr->hdr_chksum != 1 ? (int8_t)-1 : 0) &
                  NETWORK_$DO_CHKSUM) < 0) {
        hdr = fr->hdr;                              /* 0x00E75EB6 */
        if ((hdr->flags & 0x01) == 0) {             /* 0x00E75EBA */
            /*
             * 0x00E75EC0: HDR_CHKSUM(hdr, &hdr_hdr_len) - the header by
             * value, the length by reference.
             */
            chksum = HDR_CHKSUM(fr->hdr, (uint16_t *)&fr->hdr_hdr_len);
            if (chksum != fr->hdr_chksum) {         /* 0x00E75ECE */
                CRASH_SYSTEM(&ring_$rcv_chksum_status);
                /* 0x00E75EE2: unreachable, CRASH_SYSTEM does not return. */
                stats->rcvhcsum++;
                goto done;
            }
        }
    }

    /* 0x00E75EEA */
    result = true;
    stats->rcvcnt++;                        /* 0x00E75EEC */
    goto done;

error_path:
    hdr = fr->hdr;                                  /* 0x00E75EF4 */

    /*
     * 0x00E75EF8-0x00E75F08: message types 1 and 3 are the "network failure"
     * probes; record who sent them instead of counting an error.
     */
    if (hdr->msg_type == 1 || hdr->msg_type == 3) {
        NETWORK_$FAILURE_REC.timestamp = TIME_$CURRENT_CLOCKH;   /* 0x00E75F10 */
        NETWORK_$FAILURE_REC.error_info = fr->hdr->src_id;       /* 0x00E75F1C */
        NETWORK_$FAILURE_REC.flag = (int8_t)0xFF;                        /* 0x00E75F22 */
        NETWORK_$FAILURE_REC.node_id = fr->hdr->msg_type;        /* 0x00E75F2A */
        goto done;
    }

    /*
     * 0x00E75F32-0x00E75F4A: packets from the software diagnostic (both
     * flag bits 1 and 4) also bump the RING_$SWDIAG_DATA mirror counters.
     */
    hdr = fr->hdr;
    if ((hdr->flags & 0x02) != 0 && (hdr->flags & 0x10) != 0) {
        is_swdiag = true;
    } else {
        is_swdiag = false;      /* "move.b D2b,D0b": D2 is still false here */
    }

    /* 0x00E75F4C */
    if ((fr->rcv_status & 0x0400) != 0 || (fr->rcv_status & 0x0800) != 0) {
        if ((fr->rcv_status & 0x0400) != 0) {
            RING_$RCV_ESB++;                        /* 0x00E75F66 */
        }
        if ((fr->rcv_status & 0x0800) != 0) {
            RING_$RCV_BIPHASE++;                    /* 0x00E75F76 */
        }
        stats->rcvpkt++;                  /* 0x00E75F7C */
        if (is_swdiag < 0) {
            swdiag->rcvpkt++;             /* 0x00E75F86 */
        }
        goto done;
    }

    /* 0x00E75F8E */
    if ((fr->rcv_status & 0x0200) != 0) {
        stats->rcvtim++;
        if (is_swdiag < 0) {
            swdiag->rcvtim++;
        }
        goto done;
    }

    /* 0x00E75FAA */
    if ((fr->rcv_status & 0x0040) != 0) {
        CRASH_SYSTEM(&ring_$rcv_stat40_status);
        /* 0x00E75FBE: unreachable. */
        stats->rcvbus++;
        goto done;
    }

    /* 0x00E75FC4 */
    if ((fr->rcv_status & 0x0020) != 0) {
        stats->rcveor++;
        if (is_swdiag < 0) {
            swdiag->rcveor++;
        }
        goto done;
    }

    /* 0x00E75FDC: "btst.l D2,D1" with D2 still zero, i.e. bit 0. */
    if ((fr->rcv_status & 0x0001) != 0) {
        stats->rcvapar++;
        if (is_swdiag < 0) {
            swdiag->rcvapar++;
        }
        goto done;
    }

    /*
     * 0x00E75FF2: unlike every case above, this one does NOT end the chain -
     * it falls through into the 0x0080 test at 0x00E76008.
     */
    if ((fr->rcv_status & 0x0100) != 0) {
        stats->rcvcrc++;
        if (is_swdiag < 0) {
            swdiag->rcvcrc++;
        }
    }

    /*
     * 0x00E76008: "tst.b (-0x2f,A2)" - the LOW byte of the status word, so
     * this is bit 7 of rcv_status.
     */
    if ((int8_t)(fr->rcv_status & 0x00FF) < 0) {
        stats->rcvxerr++;
        if (is_swdiag < 0) {
            swdiag->rcvxerr++;
        }
        goto done;
    }

    /* 0x00E7601C */
    if ((fr->rcv_status & 0x0008) != 0) {
        stats->rcvmodem++;
        if (is_swdiag < 0) {
            swdiag->rcvmodem++;
        }
    }

done:
    return result;                                  /* 0x00E76032 */
}
