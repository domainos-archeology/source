/*
 * network_$do_request - Send network command and wait for response
 *
 * Internal helper function that handles the complete request/response cycle
 * for network operations. Allocates a temporary socket, sends the command
 * packet, waits for the response, validates the response type, and handles
 * retries on timeout.
 *
 * The function uses two internal helpers:
 *   - network_$send_request (0x00E0F5F4): Builds and sends the request packet
 *   - network_$wait_response (0x00E0F746): Waits for response with timeout
 *
 * Response validation: The response type (first word of response) must equal
 * the command type (first word of request) + 1 to be considered valid.
 *
 * Original address: 0x00E0F86C
 */

#include "network/network_internal.h"
#include "sock/sock.h"
#include "pkt/pkt.h"
#include "ec/ec.h"
#include "misc/misc.h"

/*
 * Status codes
 */
/* status_$network_receive_process_failed_to_start,
 * status_$network_unexpected_reply_type:
 * network/network.h */

/*
 * Error constant for crash on socket allocation failure
 */
static const status_$t Network_No_Available_Socket_Err = status_$network_receive_process_failed_to_start;

/*
 * network_$do_request - Send a network command and receive response
 *
 * @param net_handle     Network handle/connection (contains node info at offset 4)
 * @param cmd_buf        Command buffer to send (first word is command type)
 * @param cmd_len        Command length in bytes
 * @param param4         Parameter passed to send helper (typically 0)
 * @param param5         Parameter passed to send helper (typically 0)
 * @param check_flag     If negative, enables early-out check after 2 retries
 * @param resp_buf       Response buffer output
 * @param resp_info      Response info output (length at offset 0, status at offset 2)
 * @param status_ret     Output: status code
 */
void network_$do_request(void *net_handle, void *cmd_buf, int16_t cmd_len,
                         uint32_t param4, uint16_t param5, int16_t check_flag,
                         void *resp_buf, void *resp_info, status_$t *status_ret)
{
    int16_t sock_num;
    int16_t pkt_id;
    int32_t event_count;
    int16_t retry_count;
    int8_t result;
    uint16_t max_retries;
    int16_t timeout_value;
    uint32_t data_bufs[6];      /* Buffer for received data pointers */
    uint16_t data_len;
    int16_t *cmd_ptr;
    network_$reply_hdr_t *reply;
    uint32_t target_node;

    retry_count = 0;

    /*
     * Allocate a temporary socket for this request.
     * Protocol 2, buffer_pages 0, max queue 0x400.
     * Packed format: (protocol << 16) | buffer_pages = (2 << 16) | 0 = 0x20000
     */
    result = SOCK_$ALLOCATE((uint16_t *)&sock_num, 0x20000, 0x400);
    if (result >= 0) {
        /* No free sockets available - crash the system */
        CRASH_SYSTEM(&Network_No_Available_Socket_Err);
    }

    /*
     * Get the initial event count for this socket:
     *   0x00E0F8B8  move.w D4w,D0w
     *   0x00E0F8BA  movea.l #0xe28db4,A0
     *   0x00E0F8C2  lsl.l #0x2,D0 / lea (0x0,A0,D0*0x1),A1
     *   0x00E0F8C8  movea.l (-0x4,A1),A0 / move.l (A0),D0 / addq.l #0x1,D0
     * i.e. SOCK_$DATA.socket_ptr[sock_num] (the -4 is the table's bias; the
     * old SOCK_$SOCKET_PTR[sock_num] read one slot too far).
     */
    event_count = SOCK_$DATA.socket_ptr[sock_num]->ec.value + 1;

    /*
     * Get a unique packet ID for this request.
     */
    pkt_id = PKT_$NEXT_ID();

    /*
     * Main request/response loop with retry handling.
     */
    while (1) {
        /*
         * Send the request packet.
         * This builds and transmits the packet, returning retry and timeout info.
         */
        network_$send_request(net_handle, sock_num, pkt_id,
                              (int16_t *)cmd_buf, cmd_len, param4, param5,
                              &max_retries, &timeout_value, status_ret);

        /* Check if send failed */
        if (*status_ret != status_$ok) {
            break;
        }

        /*
         * Wait for the response with timeout.
         * The timeout is adjusted by adding NETWORK_$SERVICE_TIME.
         * Returns 0xFF on success (packet received), 0 on timeout.
         */
        result = network_$wait_response(sock_num, pkt_id,
                                        timeout_value + NETWORK_$SERVICE_TIME,
                                        &event_count, (int16_t *)resp_buf,
                                        (int16_t *)resp_info, data_bufs, &data_len);

        if (result < 0) {
            /*
             * Response received successfully.
             * Mark the target node as visible (responding).
             */
            target_node = *((uint32_t *)net_handle + 1);
            PKT_$NOTE_VISIBLE(target_node, 0xFF);

            /*
             * If we received data buffers, release them.
             */
            if (data_bufs[0] != 0) {
                PKT_$DUMP_DATA(data_bufs, data_len);
            }

            /*
             * Validate the response type.
             * Response type (first word of resp_buf) must equal
             * command type (first word of cmd_buf) + 1.
             */
            /*
             * 0x00E0F9CC-0x00E0F9E6.  Both the type word AND the status
             * longword come out of the REPLY BUFFER (A4 = the 0x1A parameter);
             * the status is the unaligned longword at resp_buf + 2, not
             * anything in resp_info (bead source-54rh).
             */
            cmd_ptr = (int16_t *)cmd_buf;
            reply = (network_$reply_hdr_t *)resp_buf;

            if (reply->reply_type == *cmd_ptr + 1) {
                *status_ret = reply->status;
            } else {
                /* Unexpected response type */
                *status_ret = status_$network_unexpected_reply_type;
            }
            break;
        }

        /*
         * Timeout occurred - increment retry counter.
         */
        retry_count++;

        /*
         * Check if we've exceeded the maximum retry count.
         * Don't give up if the target is our mother node.
         */
        /*
         * 0x00E0F95A-0x00E0F964:
         *   clr.l D1 / move.w D6w,D0w / move.w D2w,D1w / ext.l D0 / cmp.l D1,D0
         * retry_count is SIGN-extended and max_retries ZERO-extended before
         * the 32-bit compare; the default C promotions of int16_t and uint16_t
         * do exactly that, so neither operand is cast here.
         */
        target_node = *((uint32_t *)net_handle + 1);
        if (retry_count >= max_retries && target_node != NETWORK_$MOTHER_NODE) {
            *status_ret = status_$network_remote_node_failed_to_respond;
            PKT_$NOTE_VISIBLE(target_node, 0);
            break;
        }

        /*
         * If check_flag is negative and we've done 2 retries,
         * check if the node is likely to answer before continuing.
         * Skip this check for the mother node.
         */
        if (check_flag < 0 && retry_count == 2 && target_node != NETWORK_$MOTHER_NODE) {
            result = PKT_$LIKELY_TO_ANSWER(net_handle, status_ret);
            if (result >= 0) {
                /* Node is not likely to answer - give up */
                break;
            }
        }

        /* Continue with next retry */
    }

    /*
     * Clean up: close the temporary socket.
     */
    SOCK_$CLOSE((uint16_t)sock_num);
}
