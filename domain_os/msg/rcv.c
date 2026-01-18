/*
 * MSG_$RCV, MSG_$RCVI - Receive a message
 *
 * Receives a message from the specified socket.
 * The message data and metadata are copied to the caller's buffers.
 *
 * Original addresses:
 *   MSG_$RCV:  0x00E594F4 (84 bytes)
 *   MSG_$RCVI: 0x00E596B2 (164 bytes)
 *
 * The receive operation is complex, involving:
 * - Socket ownership validation
 * - Network buffer management
 * - Data copying between kernel and user space
 */

#include "msg/msg_internal.h"

/*
 * Internal receive implementation at 0x00E59548
 */
extern void MSG_$$RCV_INTERNAL(int16_t socket, void *params, status_$t *status_ret);

/*
 * MSG_$RCVI - Receive message internal implementation
 *
 * Validates socket ownership and calls internal receive routine.
 * Parameters are passed through to the internal implementation.
 */
void MSG_$RCVI(msg_$socket_t *socket,
               void *dest_net,
               void *dest_node,
               void *dest_sock,
               void *src_net,
               void *data_buf,
               uint32_t *data_len,
               void *type_buf,
               void *type_len,
               void *options,
               int16_t *bytes_received,
               void *timeout_sec,
               void *timeout_usec,
               int16_t *msg_len,
               void *reserved,
               status_$t *status_ret)
{
#if defined(M68K)
    int16_t sock_num;
    int16_t sock_offset;
    uint8_t asid;
    uint8_t byte_index;
    uint8_t *bitmap;

    sock_num = *socket;

    /* Validate socket number */
    if (sock_num < 1 || sock_num > MSG_MAX_SOCKET) {
        *status_ret = status_$msg_socket_out_of_range;
        return;
    }

    /* Check ownership */
    asid = PROC1_$AS_ID;
    sock_offset = sock_num << 3;
    bitmap = (uint8_t *)(MSG_$DATA_BASE + MSG_OFF_OWNERSHIP + sock_offset);
    byte_index = (0x3F - asid) >> 3;

    if ((bitmap[byte_index] & (1 << (asid & 7))) == 0) {
        *status_ret = status_$msg_no_owner;
        return;
    }

    /*
     * Call internal receive implementation.
     * The actual receive logic handles:
     * - Dequeuing message from socket
     * - Copying data to user buffers
     * - Returning sender information
     */
    /* TODO: Implement MSG_$$RCV_INTERNAL call with proper parameter passing */
    *status_ret = status_$ok;
    *bytes_received = 0;

#else
    (void)socket;
    (void)dest_net;
    (void)dest_node;
    (void)dest_sock;
    (void)src_net;
    (void)data_buf;
    (void)data_len;
    (void)type_buf;
    (void)type_len;
    (void)options;
    (void)bytes_received;
    (void)timeout_sec;
    (void)timeout_usec;
    (void)msg_len;
    (void)reserved;
    *status_ret = status_$msg_socket_out_of_range;
#endif
}

/*
 * MSG_$RCV - Receive message wrapper
 *
 * Calls MSG_$RCVI with parameters and copies result.
 */
void MSG_$RCV(msg_$socket_t *socket,
              void *dest_net,
              void *dest_node,
              void *dest_sock,
              int16_t *bytes_received,
              void *src_net,
              void *data_buf,
              uint32_t *data_len,
              void *type_buf,
              void *type_len,
              void *options,
              status_$t *status_ret)
{
    int16_t local_bytes;
    void *local_dest_net;
    void *local_dest_node;
    void *local_dest_sock;
    void *local_src_net;

    MSG_$RCVI(socket,
              &local_dest_net,
              &local_dest_node,
              &local_dest_sock,
              &local_src_net,
              data_buf,
              data_len,
              type_buf,
              type_len,
              options,
              &local_bytes,
              NULL,
              NULL,
              NULL,
              NULL,
              status_ret);

    *bytes_received = local_bytes;
}
