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
 * Error status for socket allocation failure
 */
static status_$t sock_alloc_error = 0x0011000C;  /* No socket available */

void PKT_$SAR_INTERNET(uint32_t routing_key, uint32_t dest_node, uint16_t dest_sock,
                       void *pkt_info, int16_t timeout,
                       void *req_template, uint16_t req_tpl_len,
                       void *req_data, uint16_t req_data_len,
                       void *resp_buf, char *resp_tpl_buf, uint16_t resp_tpl_max,
                       uint16_t *resp_tpl_len, void *resp_data_buf, uint16_t resp_data_max,
                       uint16_t *resp_data_len, status_$t *status_ret)
{
    int8_t result;
    uint16_t sock_num;
    int16_t request_id;
    int16_t retry_num;
    uint16_t max_retries;
    int32_t wait_val;
    int32_t timeout_val;
    int32_t quit_check_val;
    ec_$eventcount_t *sock_ec;
    void *recv_pkt;
    status_$t local_status;
    uint16_t len_out[2];
    int16_t recv_id;
    uint16_t recv_tpl_len;
    uint16_t copy_len;
    uint32_t recv_ppn;
    uint32_t data_buffers[10];
    uint32_t addr_info[2];
    int16_t wait_result;
    int8_t got_response;

    /* Allocate a socket for receiving response */
    result = SOCK_$ALLOCATE(&sock_num, 0x20001, 0x10400);
    if (result >= 0) {
        CRASH_SYSTEM(&sock_alloc_error);
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

    /* Generate request ID */
    request_id = PKT_$NEXT_ID();

    /* Get initial wait value (00e71f24 move.l (A1),D6 / 00e71f2c addq.l #1,D6) */
    wait_val = sock_ec->value + 1;

    /*
     * Get quit check value for current address space
     * 00e71f30  movea.l #0xe222ba,A4     ; FIM_$QUIT_VALUE
     * 00e71f36  move.l (0x0,A4,D7w*0x1),D7   ; D7 = PROC1_$AS_ID * 4
     * 00e71f3a  addq.l #0x1,D7
     */
    quit_check_val = (int32_t)FIM_$QUIT_VALUE[PROC1_$AS_ID] + 1;

    /* Set up address info for visibility tracking */
    addr_info[0] = routing_key;
    addr_info[1] = dest_node;

    retry_num = 1;

    /* Get max retries from pkt_info (offset 0x08), 0 means use 0xFFFF */
    if (*(int16_t *)((char *)pkt_info + 8) == 0) {
        max_retries = 0xFFFF;
    } else {
        max_retries = *(uint16_t *)((char *)pkt_info + 8);
    }

    got_response = 0;

    /* Main send/receive loop */
    for (;;) {
        /* Send the request */
        PKT_$SEND_INTERNET(routing_key, dest_node, dest_sock,
                           (int32_t)-1, NODE_$ME, sock_num,
                           pkt_info, request_id,
                           req_template, req_tpl_len,
                           req_data, req_data_len,
                           &len_out[1], &len_out[0], status_ret);

        if (*status_ret != status_$ok) {
            goto cleanup;
        }

        /* Update max_retries on first send */
        if (max_retries == 0xFFFF) {
            max_retries = len_out[1];
        }

        /*
         * Calculate timeout.  The sum is formed in a word and then zero
         * extended before being added to the clock:
         * 00e71fac  move.w (-0x56,A6),D0w    ; len_out[0]
         * 00e71fba  add.w (0x16,A6),D0w      ; + timeout
         * 00e71fca  andi.l #0xffff,D0
         * 00e71fd0  add.l (0x00e2b0d4).l,D0  ; + TIME_$CLOCKH
         */
        timeout_val = (int32_t)(TIME_$CLOCKH +
                                (uint32_t)(uint16_t)(timeout + len_out[0]));

        /* Wait for response or timeout */
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
                goto cleanup_no_visibility;
            }

            /* Response received - increment wait value for next wait */
            wait_val++;

            /* Receive the response */
            APP_$RECEIVE(sock_num, &recv_pkt, status_ret);

            if (*status_ret == status_$ok) {
                /* Extract response template length */
                recv_tpl_len = *(uint16_t *)((char *)recv_pkt + 2);

                /* Copy template to caller's buffer */
                copy_len = recv_tpl_len;
                if (copy_len > resp_tpl_max) {
                    copy_len = resp_tpl_max;
                }
                *resp_tpl_len = copy_len;
                OS_$DATA_COPY(*(char **)((char *)recv_pkt + 0x28), resp_tpl_buf, (uint32_t)copy_len);

                /* Get response ID */
                recv_id = *(int16_t *)((char *)recv_pkt + 6);

                /* Return header buffer */
                recv_ppn = (*(uint32_t *)((char *)recv_pkt + 0x28)) & 0xFFFFFC00;
                NETBUF_$RTN_HDR(&recv_ppn);

                /* Handle data buffers */
                data_buffers[0] = *(uint32_t *)((char *)recv_pkt + 0x2C);
                if (data_buffers[0] == 0) {
                    *resp_data_len = 0;
                } else {
                    /* Copy data to caller's buffer */
                    uint16_t data_len = *(uint16_t *)((char *)recv_pkt + 4);
                    copy_len = data_len;
                    if (copy_len > resp_data_max) {
                        copy_len = resp_data_max;
                    }
                    *resp_data_len = copy_len;
                    PKT_$DAT_COPY(data_buffers, copy_len, (char *)resp_data_buf);
                    PKT_$DUMP_DATA(data_buffers, data_len);
                }

                /* Check if response matches our request */
                if (recv_id == request_id) {
                    got_response = (int8_t)0xFF;
                    goto cleanup;
                }
            }

            /* Wrong ID or error - continue waiting */
        }

        /* Timeout - check if we should retry */
        if ((int32_t)retry_num == (int32_t)max_retries) {
            /* Max retries reached */
            if (retry_num > 2) {
                PKT_$NOTE_VISIBLE(dest_node, 0);
            }
            *(int16_t *)((char *)pkt_info + 8) = retry_num;
            *status_ret = status_$network_remote_node_failed_to_respond;
            goto cleanup_no_visibility;
        }

        /* After 2 retries, check if node is likely to answer */
        if (retry_num == 2) {
            result = PKT_$LIKELY_TO_ANSWER(addr_info, status_ret);
            if (result >= 0) {
                /* Node unlikely to answer */
                *(int16_t *)((char *)pkt_info + 8) = retry_num;
                *status_ret = status_$network_remote_node_failed_to_respond;
                goto cleanup_no_visibility;
            }
        }

        retry_num++;
    }

cleanup:
    /* Update visibility based on result */
    if (*status_ret == status_$ok) {
        PKT_$NOTE_VISIBLE(dest_node, (int8_t)0xFF);
    }

cleanup_no_visibility:
    /* Close the socket */
    SOCK_$CLOSE(sock_num);
}
