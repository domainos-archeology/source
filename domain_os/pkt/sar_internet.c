/*
 * PKT_$SAR_INTERNET - Send and receive internet packet
 *
 * Sends a packet and waits for a response. Handles retries and
 * node visibility tracking.
 *
 * The algorithm:
 * 1. Allocate a socket for receiving the response
 * 2. Generate a unique request ID
 * 3. Loop sending and waiting for response:
 *    a. Send request via PKT_$SEND_INTERNET
 *    b. Wait on socket EC, time EC, and quit EC
 *    c. If response received with matching ID, success
 *    d. If timeout, retry (up to max retries)
 *    e. If quit requested, abort
 * 4. After 2 retries with no response, check if node is likely to answer
 * 5. Update visibility tracking based on result
 * 6. Close socket and return
 *
 * Original address: 0x00E71EC4
 */

#include "pkt/pkt_internal.h"
#include "misc/crash_system.h"

/*
 * pkt_$sar_no_socket_status - the CRASH_SYSTEM operand of the failed
 * SOCK_$ALLOCATE, "pea (0x288,PC)" at 0x00E71EF6 -> 0x00E71EF8 + 0x288 =
 * 0x00E72180.  The image bytes there are 00 11 00 05, i.e.
 * status_$network_receive_process_failed_to_start; SR10.4 renames the same
 * code "no available socket", which is what this call site means.
 */
static status_$t pkt_$sar_no_socket_status =
    status_$network_receive_process_failed_to_start;

void PKT_$SAR_INTERNET(uint32_t routing_key, uint32_t dest_node, uint16_t dest_sock,
                       void *pkt_info, int16_t timeout,
                       void *req_template, uint16_t req_tpl_len,
                       void *req_data, uint16_t req_data_len,
                       pkt_$sar_result_t *resp_buf,
                       char *resp_tpl_buf, uint16_t resp_tpl_max,
                       uint16_t *resp_tpl_len, void *resp_data_buf, uint16_t resp_data_max,
                       uint16_t *resp_data_len, status_$t *status_ret)
{
    int8_t result;                  /* D0b */
    uint16_t sock_num;              /* A6-0x60, then D5 */
    int16_t request_id;             /* D4 */
    int16_t retry_num;              /* D3 */
    uint16_t max_retries;           /* A6-0x52 */
    int32_t wait_val;               /* D6 */
    int32_t timeout_val;            /* A6-0x40 */
    int32_t quit_check_val;         /* A6-0x44 */
    ec_$eventcount_t *sock_ec;      /* A6-0x3C */
    pkt_$net_addr_t addr_info;      /* A6-0x38 .. A6-0x31 */
    uint16_t retry_hint;            /* A6-0x54, PKT_$SEND_INTERNET output */
    uint16_t rtt_hint;              /* A6-0x56, PKT_$SEND_INTERNET output */
    app_$receive_rec_t rec;         /* A6-0x30 */
    const app_$reply_hdr_t *reply;  /* A0 */
    int16_t recv_id;                /* D2 */
    uint16_t copy_len;              /* D1, then D0 */
    int16_t wait_result;            /* D0w */

    /* 0x00E71ED4 - 0x00E71EEE */
    result = SOCK_$ALLOCATE(&sock_num, 0x20001, 0x10400);
    if (result >= 0) {
        CRASH_SYSTEM(&pkt_$sar_no_socket_status);
    }

    /*
     * Get socket's event count
     * 00e71f04  movea.l #0xe28db4,A0
     * 00e71f0c  lsl.l #0x2,D0            ; D0 = sock_num * 4
     * 00e71f0e  lea (0x0,A0,D0*0x1),A1
     * 00e71f12  move.l (-0x4,A1),(-0x3c,A6)
     * i.e. *(0xe28db0 + sock_num*4) == SOCK_$EVENT_COUNTERS[sock_num - 1].
     */
    sock_ec = SOCK_$EVENT_COUNTERS[sock_num - 1];

    /* 0x00E71F18 */
    request_id = PKT_$NEXT_ID();

    /* 0x00E71F24 / 0x00E71F2C: move.l (A1),D6 / addq.l #1,D6 */
    wait_val = sock_ec->value + 1;

    /*
     * Get quit check value for current address space
     * 00e71f30  movea.l #0xe222ba,A4     ; FIM_$QUIT_VALUE
     * 00e71f36  move.l (0x0,A4,D7w*0x1),D7   ; D7 = PROC1_$AS_ID * 4
     * 00e71f3a  addq.l #0x1,D7
     */
    quit_check_val = (int32_t)FIM_$QUIT_VALUE[PROC1_$AS_ID] + 1;

    /*
     * 0x00E71F40 - 0x00E71F46: the two argument longwords are copied into an
     * adjacent pair of locals so their address can be handed to
     * PKT_$LIKELY_TO_ANSWER later ("pea (-0x38,A6)" at 0x00E7204E).
     */
    addr_info.network = routing_key;
    addr_info.node = dest_node;

    retry_num = 1;

    /*
     * 0x00E71F4C - 0x00E71F66: a zero retry limit in the caller's packet-info
     * record means "not set yet"; the first PKT_$SEND_INTERNET fills it in
     * from its retry_hint output.
     */
    if (((pkt_$info_t *)pkt_info)->retry_limit == 0) {
        max_retries = 0xFFFF;
    } else {
        max_retries = ((pkt_$info_t *)pkt_info)->retry_limit;
    }

    /* Main send/receive loop (0x00E71F66) */
    for (;;) {
        /* 0x00E71F66 - 0x00E71FA8 */
        PKT_$SEND_INTERNET(routing_key, dest_node, dest_sock,
                           (int32_t)-1, NODE_$ME, sock_num,
                           pkt_info, (uint16_t)request_id,
                           req_template, req_tpl_len,
                           req_data, (int16_t)req_data_len,
                           &retry_hint, &rtt_hint, status_ret);

        /* 0x00E71FB4 */
        if (*status_ret != status_$ok) {
            goto close_socket;
        }

        /* 0x00E71FBE - 0x00E71FC6 */
        if (max_retries == 0xFFFF) {
            max_retries = retry_hint;
        }

        /*
         * Calculate timeout.  The sum is formed in a word and then zero
         * extended before being added to the clock:
         * 00e71fac  move.w (-0x56,A6),D0w    ; rtt_hint
         * 00e71fba  add.w (0x16,A6),D0w      ; + timeout
         * 00e71fca  andi.l #0xffff,D0
         * 00e71fd0  add.l (0x00e2b0d4).l,D0  ; + TIME_$CLOCKH
         */
        timeout_val = (int32_t)(TIME_$CLOCKH +
                                (uint32_t)(uint16_t)(timeout + rtt_hint));

        /* Wait for response or timeout (0x00E71FE8) */
        for (;;) {
            /*
             * Both arrays go on the stack by value; arguments are pushed
             * right-to-left so the pointers end up at the lower addresses
             * (00e71fe8 - 00e72010):
             *   00e71fe8  move.l (-0x44,A6),-(SP)   vals[2] = quit_check_val
             *   00e71ff0  move.l D0,-(SP)           vals[1] = timeout_val
             *   00e71ff2  move.l D6,-(SP)           vals[0] = wait_val
             *   00e72002  pea (0x0,A4,D2w)  A4 = 0xe22002, D2 = AS_ID*4*3
             *                                       ecs[2] = &FIM_$QUIT_EC[AS_ID]
             *   00e72006  move.l #0xe2b0d4,-(SP)    ecs[1] = &TIME_$CLOCKH
             *   00e7200c  move.l (-0x3c,A6),-(SP)   ecs[0] = sock_ec
             *   00e72010  jsr EC_$WAIT              ; 0-based index in D0
             * The AS_ID scaling is x4 then x3 = x12, i.e. one 12-byte
             * ec_$eventcount_t per address space.
             */
            wait_result = EC_$WAIT(
                (ec_$wait_ecs_t){{ sock_ec,
                                   (ec_$eventcount_t *)&TIME_$CLOCKH,
                                   &FIM_$QUIT_EC[PROC1_$AS_ID] }},
                (ec_$wait_vals_t){{ wait_val, timeout_val, quit_check_val }});

            if (wait_result == 1) {
                /* Timeout (00e7201a cmpi.w #0x1,D0w) */
                break;
            }

            if (wait_result == 2) {
                /* Quit requested (00e72076 cmpi.w #0x2,D0w) */
                FIM_$QUIT_VALUE[PROC1_$AS_ID] =
                    (uint32_t)FIM_$QUIT_EC[PROC1_$AS_ID].value;
                *status_ret = 0x120010;  /* Quit status */
                goto check_visible;
            }

            /* 0x00E720A8: response queued, bump the awaited socket value */
            wait_val++;

            /* 0x00E720AA - 0x00E720BA */
            APP_$RECEIVE(sock_num, &rec, status_ret);

            /* 0x00E720BE: an error just goes back to waiting */
            if (*status_ret == status_$ok) {
                /* 0x00E720C4 */
                reply = (const app_$reply_hdr_t *)ARCH_VA_TO_PTR(rec.reply);

                /* 0x00E720C8 - 0x00E720DA: bls, so the compare is unsigned */
                copy_len = reply->template_len;
                if (copy_len > resp_tpl_max) {
                    copy_len = resp_tpl_max;
                }
                *resp_tpl_len = copy_len;

                /*
                 * 0x00E720DC - 0x00E720F0: the source is the record's data
                 * pointer at +0x04, not the reply header.
                 */
                OS_$DATA_COPY(ARCH_VA_TO_PTR(rec.data), resp_tpl_buf,
                              (uint32_t)copy_len);

                /* 0x00E720F4 */
                recv_id = (int16_t)reply->request_id;

                /* 0x00E720FC: &rec.data, unmasked */
                NETBUF_$RTN_HDR(&rec.data);

                /* 0x00E72108 */
                if (rec.data_pages[0] == 0) {
                    *resp_data_len = 0;         /* 0x00E72150 */
                } else {
                    /* 0x00E7210E - 0x00E72120: bls, unsigned */
                    copy_len = reply->data_len;
                    if (copy_len > resp_data_max) {
                        copy_len = resp_data_max;
                    }
                    *resp_data_len = copy_len;

                    /* 0x00E72122 - 0x00E72134 */
                    PKT_$DAT_COPY(rec.data_pages, (int16_t)copy_len,
                                  (char *)resp_data_buf);

                    /*
                     * 0x00E72138 - 0x00E7214C: the length is re-read from the
                     * reply header, so the full received length is released
                     * even when the caller's buffer clamped the copy.
                     */
                    PKT_$DUMP_DATA(rec.data_pages, (int16_t)reply->data_len);
                }

                /* 0x00E72152 */
                if (recv_id == request_id) {
                    goto check_visible;
                }
            }
        }

        /* 0x00E72020 - 0x00E7202C: the compare is done in longs */
        if ((int32_t)retry_num == (int32_t)max_retries) {
            /* 0x00E7202E: more than two attempts means the node went quiet */
            if (retry_num > 2) {
                PKT_$NOTE_VISIBLE(dest_node, 0);   /* 0x00E72036 clr.w -(SP) */
            }
            goto no_answer;
        }

        /* 0x00E72046 - 0x00E7205C */
        if (retry_num == 2) {
            result = PKT_$LIKELY_TO_ANSWER(&addr_info, status_ret);
            if (result >= 0) {
                /* Node unlikely to answer */
                goto no_answer;
            }
        }

        retry_num++;                            /* 0x00E72070 */
    }

no_answer:
    /*
     * 0x00E7205E - 0x00E72066: the attempt count is reported through the
     * TENTH argument, "movea.l (0x24,A6),A0 / move.w D3w,(0x8,A0)" - not
     * through the packet-info record the retry limit was read from.
     */
    resp_buf->attempts = (uint16_t)retry_num;
    *status_ret = status_$network_remote_node_failed_to_respond;

check_visible:
    /*
     * 0x00E72158 - 0x00E7216A.  The two error exits reach the "bne" at
     * 0x00E7215A with the flags of their own non-zero status store, so only
     * a clean status runs the call.
     */
    if (*status_ret == status_$ok) {
        PKT_$NOTE_VISIBLE(dest_node, (boolean)0xFF);  /* 0x00E7215E st -(SP) */
    }

close_socket:
    /* 0x00E7216C */
    SOCK_$CLOSE(sock_num);
}
