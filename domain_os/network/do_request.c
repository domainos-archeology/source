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
#define status_$network_no_available_sockets            0x00110005
#define status_$network_unexpected_reply_type           0x0011000B
#define status_$network_remote_node_failed_to_respond   0x00110007

/*
 * Error constant for crash on socket allocation failure
 */
static const status_$t Network_No_Available_Socket_Err = status_$network_no_available_sockets;

/*
 * Network data area globals (accessed via A5 in original code)
 */
extern uint32_t NETWORK_$MOTHER_NODE;       /* 0xE24C0C (+0x310) - mother node ID */
extern int16_t  NETWORK_$RETRY_TIMEOUT;     /* 0xE24C18 (+0x31c) - timeout offset for retries */

/*
 * Socket descriptor array
 * Each socket has a pointer to its control structure
 */
extern void *SOCK_$SOCKET_PTR[];            /* 0xE28DB4 - socket pointer array */

/*
 * Forward declarations for internal helpers
 */
void network_$send_request(void *net_handle, int16_t sock_num, int16_t pkt_id,
                           int16_t *cmd_buf, int16_t cmd_len, int16_t param_hi,
                           uint32_t param_lo, uint16_t *retry_count_out,
                           int16_t *timeout_out, status_$t *status_ret);

int8_t network_$wait_response(int16_t sock_num, int16_t pkt_id, uint16_t timeout,
                              int32_t *event_count, int16_t *resp_buf,
                              int16_t *resp_len_out, uint32_t *data_bufs,
                              uint16_t *data_len_out);

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
    int16_t *resp_ptr;
    uint32_t target_node;

    retry_count = 0;

    /*
     * Allocate a temporary socket for this request.
     * Protocol 2, buffer_pages 0, max queue 0x400.
     */
    result = SOCK_$ALLOCATE((uint16_t *)&sock_num, 2, 0, 0x400);
    if (result >= 0) {
        /* No free sockets available - crash the system */
        CRASH_SYSTEM(&Network_No_Available_Socket_Err);
    }

    /*
     * Get the initial event count for this socket.
     * The socket structure is accessed via SOCK_$SOCKET_PTR[sock_num].
     * The event count is at offset 0 in the structure, and we add 1.
     */
    event_count = *((int32_t *)SOCK_$SOCKET_PTR[sock_num]) + 1;

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
                              (int16_t *)cmd_buf, cmd_len,
                              (int16_t)(param4 >> 16),
                              ((param4 & 0xFFFF) << 16) | param5,
                              &max_retries, &timeout_value, status_ret);

        /* Check if send failed */
        if (*status_ret != status_$ok) {
            break;
        }

        /*
         * Wait for the response with timeout.
         * The timeout is adjusted by adding NETWORK_$RETRY_TIMEOUT.
         * Returns 0xFF on success (packet received), 0 on timeout.
         */
        result = network_$wait_response(sock_num, pkt_id,
                                        timeout_value + NETWORK_$RETRY_TIMEOUT,
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
            cmd_ptr = (int16_t *)cmd_buf;
            resp_ptr = (int16_t *)resp_buf;

            if (*resp_ptr == *cmd_ptr + 1) {
                /* Valid response - extract status from response info */
                *status_ret = *((status_$t *)((int16_t *)resp_info + 1));
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
        target_node = *((uint32_t *)net_handle + 1);
        if (retry_count >= (int16_t)max_retries && target_node != NETWORK_$MOTHER_NODE) {
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
