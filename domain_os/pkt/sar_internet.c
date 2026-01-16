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

    /* Get socket's event count */
    /* TODO: Get proper socket event count from socket table */
    /* sock_ec = *(ec_$eventcount_t **)(&SOCK_EC_ARRAY + sock_num * 4); */
    sock_ec = NULL;  /* Placeholder */

    /* Generate request ID */
    request_id = PKT_$NEXT_ID();

    /* Get initial wait value */
    if (sock_ec != NULL) {
        wait_val = *(int32_t *)sock_ec + 1;
    } else {
        wait_val = 1;
    }

    /* Get quit check value for current address space */
    quit_check_val = FIM_$QUIT_VALUE[PROC1_$AS_ID] + 1;

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

        /* Calculate timeout */
        timeout_val = TIME_$CLOCKH + (uint32_t)(timeout + len_out[0]);

        /* Wait for response or timeout */
        for (;;) {
            ec_$eventcount_t *ecs[3];
            int32_t wait_vals[3];

            /* Set up event counts to wait on */
            ecs[0] = sock_ec;
            ecs[1] = (ec_$eventcount_t *)&TIME_$CLOCKH;
            ecs[2] = (ec_$eventcount_t *)(FIM_$QUIT_EC + PROC1_$AS_ID * 3);
            wait_vals[0] = wait_val;
            wait_vals[1] = timeout_val;
            wait_vals[2] = quit_check_val;

            wait_result = EC_$WAIT(ecs, wait_vals);

            if (wait_result == 1) {
                /* Timeout */
                break;
            }

            if (wait_result == 2) {
                /* Quit requested */
                FIM_$QUIT_VALUE[PROC1_$AS_ID] = *(uint32_t *)(FIM_$QUIT_EC + PROC1_$AS_ID * 3);
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
