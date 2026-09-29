/*
 * PKT_$SEND_INTERNET - Build and transmit one Domain internet packet
 *
 * Copies the caller's data into network pages, then loops: fetch a header
 * buffer, build the internet header into it, hand it to NET_IO_$SEND, and on
 * a failed send return the header, wait 25000 ticks and try again.  The
 * attempt limit is pkt_info->retry_limit, or - when that is 0 or 0xFFFF -
 * whatever PKT_$BLD_INTERNET_HDR wrote into *retry_hint (always 5).
 *
 * Fifteen arguments plus a 2-byte Pascal function-result slot the caller
 * discards; callers pop 0x34 bytes.  Prologue argument offsets:
 *   0x08 routing_key   0x0C dest_node   0x10 dest_sock  0x12 src_node_or
 *   0x16 src_node      0x1A src_sock    0x1C pkt_info   0x20 request_id
 *   0x22 template      0x26 template_len 0x28 data      0x2C data_len
 *   0x2E retry_hint    0x32 timeout_out  0x36 status_ret
 *
 * Original address: 0x00E1264E (406 bytes)
 * Module base: A5 = 0x00E24C9C = PKT_$DATA (0x00E12656 "lea (0xe24c9c).l,A5")
 */

#include "pkt/pkt_internal.h"

/*
 * TIME_$WAIT's first argument is a Pascal `const` delay type passed by
 * reference: 0x00E12790 "pea (0x52,PC)" resolves to 0x00E12790 + 2 + 0x52 =
 * 0x00E127E4, a zero word sitting between this function's rts and
 * PKT_$DUMP_DATA.  Zero is the relative-delay type.
 */
static uint16_t pkt_$wait_relative = 0;

/* 0x00E12782 "move.w #0x61a8,(-0x18,A6)" - the low half of the 48-bit delay */
#define PKT_RETRY_WAIT_TICKS 0x61A8 /* 25000 */

/*
 * 0x00E127A2 "cmpi.l #0xd0003,D0" - "quit while waiting for event" from the
 * OS time manager (module 0x0D).
 */

void PKT_$SEND_INTERNET(uint32_t routing_key, uint32_t dest_node, uint16_t dest_sock,
                        int32_t src_node_or, uint32_t src_node, uint16_t src_sock,
                        void *pkt_info, uint16_t request_id,
                        void *template, uint16_t template_len,
                        void *data, int16_t data_len,
                        uint16_t *retry_hint, uint16_t *timeout_out,
                        status_$t *status_ret)
{
    /* A4 = pkt_info (0x00E1265C), the only field this function reads. */
    const pkt_$info_t *info = (const pkt_$info_t *)pkt_info;

    uint32_t  data_pages[PKT_MAX_DATA_CHUNKS];  /* A6-0x10, four longwords */
    net_io_$send_info_t send_info;              /* A6-0x14 */
    clock_t   retry_delay;                      /* A6-0x1C (long) + A6-0x18 (word) */
    uint32_t  hdr_pa;                           /* A6-0x20 */
    status_$t wait_status;                      /* A6-0x24 */
    status_$t status;                           /* A6-0x28 */
    uint32_t  hdr_va;                           /* A6-0x30 */
    int16_t   port;                             /* A6-0x32 */
    uint16_t  total_len;                        /* A6-0x38 */
    uint32_t  saved_hdr_va;                     /* D2 */
    uint16_t  attempt;                          /* D3 */
    uint16_t  max_attempts;                     /* D4 */

    /*
     * 0x00E12670 "clr.l (-0x10,A6)" - only the first page slot is cleared;
     * PKT_$COPY_TO_PA and PKT_$DUMP_DATA both stop at the first zero entry.
     */
    data_pages[0] = 0;

    /*
     * 0x00E12674  cmpi.w #0x200,D5w      D5 = template_len
     * 0x00E12678  bls.b
     * 0x00E1267C  move.l #0x11000a,(A0)
     */
    if (template_len > 0x200) {
        *status_ret = status_$network_msg_header_too_big;
        return;
    }

    /*
     * 0x00E12686  tst.w D6w / ble        D6 = data_len, a SIGNED test
     * 0x00E1268A - 0x00E1269C: result slot, then status_ret (the caller's own
     * out-parameter, not the local status), &data_pages, data_len, data.
     * The original does NOT test the status afterwards; it falls straight
     * through to the retry-limit setup at 0x00E126A0.
     */
    if (data_len > 0) {
        PKT_$COPY_TO_PA((char *)data, (uint16_t)data_len, data_pages, status_ret);
    }

    /*
     * 0x00E126A0  tst.w (0x8,A4)
     * 0x00E126A4  bne.b   -> take the record's own limit
     * 0x00E126A6  move.w #-0x1,D4w
     */
    if (info->retry_limit == 0) {
        max_attempts = 0xFFFF;
    } else {
        max_attempts = info->retry_limit;
    }

    attempt = 0;                                /* 0x00E126B0 "clr.w D3w" */

    do {
        attempt++;                              /* 0x00E126B4 "addq.w #0x1,D3w" */

        /*
         * 0x00E126B6 - 0x00E126C8: &hdr_pa, &hdr_va, &dest_node.  The node is
         * passed by reference out of the argument block itself
         * ("pea (0xc,A6)").
         */
        NETWORK_$GETHDR(&dest_node, &hdr_va, &hdr_pa);

        /* 0x00E126CC "move.l (-0x30,A6),D2" - keep the VA in a register so it
         * survives the callees that write through &hdr_va. */
        saved_hdr_va = hdr_va;

        /*
         * 0x00E126D0 - 0x00E12712: a word result slot then seventeen
         * arguments, popped with "lea (0x3c,SP),SP".  Note that
         * 0x00E126DC "pea (A3)" and 0x00E126D8 "move.l (0x32,A6),-(SP)" pass
         * this function's own retry_hint and timeout_out arguments straight
         * through - the builder writes 5 and 4 into them - and that the header
         * buffer goes by value (0x00E126E2 "pea (A2)").
         */
        PKT_$BLD_INTERNET_HDR(routing_key, dest_node, dest_sock,
                              src_node_or, src_node, src_sock,
                              pkt_info, request_id,
                              template, template_len, (uint16_t)data_len,
                              &port, (pkt_$hdr_t *)ARCH_VA_TO_PTR(saved_hdr_va),
                              &total_len,
                              retry_hint, timeout_out, &status);

        if (status != status_$ok) {             /* 0x00E12716 "tst.l (-0x28,A6)" */
            break;                              /* 0x00E1271A -> 0x00E127B6 */
        }

        /*
         * 0x00E1271E  cmpi.w #-0x1,D4w
         * 0x00E12724  move.w (A3),D4w     A3 == the retry_hint argument
         */
        if (max_attempts == 0xFFFF) {
            max_attempts = *retry_hint;
        }

        /* 0x00E12726 "move.l D2,(-0x30,A6)" - restore the VA slot the builder
         * may have disturbed; NET_IO_$SEND takes its address. */
        hdr_va = saved_hdr_va;

        /*
         * 0x00E1272A - 0x00E12754: ten arguments, popped with
         * "lea (0x20,SP),SP".  Pushed right to left:
         *   0x00E1272A  pea (-0x28,A6)          &status
         *   0x00E1272E  pea (-0x14,A6)          &send_info
         *   0x00E12732  move.w (0x64,A5),-(SP)  PKT_$DATA.default_flags
         *   0x00E12736  move.w D6w,-(SP)        data_len
         *   0x00E12738  pea (-0x10,A6)          data_pages
         *   0x00E1273C  clr.l -(SP)             data_va = 0
         *   0x00E1273E  move.w (-0x38,A6),-(SP) total_len
         *   0x00E12742  move.l (-0x20,A6),-(SP) hdr_pa
         *   0x00E12746  pea (-0x30,A6)          &hdr_va
         *   0x00E1274A  move.w (-0x32,A6),-(SP) port
         */
        NET_IO_$SEND(port, &hdr_va, hdr_pa, total_len, 0,
                     data_pages, data_len, PKT_$DATA.default_flags,
                     &send_info, &status);

        /*
         * 0x00E12758  tst.l (-0x28,A6) / beq -> 0x00E127B6
         * A successful send leaves D2 alone, so the common exit below still
         * hands the header back to NETWORK_$RTNHDR.
         */
        if (status == status_$ok) {
            break;
        }

        /* 0x00E1275E - 0x00E1276E: return the header, then forget it. */
        hdr_va = saved_hdr_va;
        NETWORK_$RTNHDR(&hdr_va);
        saved_hdr_va = 0;                       /* 0x00E1276E "clr.l D2" */

        /*
         * 0x00E12770  tst.w (-0x14,A6)     / bne -> retry
         * 0x00E12776  cmpi.w #0x2000,(-0x12,A6) / beq -> give up
         */
        if (send_info.port_net == 0 && send_info.xmit_status == 0x2000) {
            break;
        }

        /*
         * 0x00E1277E  move.l D2,(-0x1c,A6)   D2 is 0 here
         * 0x00E12782  move.w #0x61a8,(-0x18,A6)
         * 0x00E12788 - 0x00E1279A: &wait_status, &retry_delay, &delay type.
         */
        retry_delay.high = 0;
        retry_delay.low  = PKT_RETRY_WAIT_TICKS;
        TIME_$WAIT(&pkt_$wait_relative, &retry_delay, &wait_status);

        /* 0x00E1279E - 0x00E127AE */
        if (wait_status == status_$time_quit_while_waiting) {
            status = wait_status;
            break;
        }

        /* 0x00E127B0 "cmp.w D4w,D3w" / 0x00E127B2 "bcs.w" - unsigned. */
    } while (attempt < max_attempts);

    /* 0x00E127B6 "tst.l D2" - a header still in hand goes back. */
    if (saved_hdr_va != 0) {
        hdr_va = saved_hdr_va;                  /* 0x00E127BA */
        NETWORK_$RTNHDR(&hdr_va);               /* 0x00E127C2 */
    }

    /* 0x00E127CA - 0x00E127D2: result slot, data_len, &data_pages. */
    PKT_$DUMP_DATA(data_pages, data_len);

    /* 0x00E127D6 "move.l (-0x28,A6),(A0)" */
    *status_ret = status;
}
