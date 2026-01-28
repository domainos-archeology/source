/*
 * MSG_$SEND_HW - Send message with hardware address routing
 *
 * Sends a message using hardware address information for routing.
 * The destination is determined by looking up the port from the
 * hardware address info provided by the caller.
 *
 * Original address: 0x00E59B14 (144 bytes)
 *
 * This function:
 * 1. Extracts network/socket from the hardware address info parameter
 * 2. Looks up the corresponding port using ROUTE_$FIND_PORT
 * 3. Calls MSG_$$SEND to perform the actual send
 */

#include "msg/msg_internal.h"

/* status_$internet_unknown_network_port is defined in route/route.h (0x2B0003) */

/*
 * Hardware address info structure layout:
 *   +0x00: reserved (6 bytes)
 *   +0x06: network (2 bytes)
 *   +0x08: socket (2 bytes, sign-extended to 32-bit for lookup)
 */

/*
 * MSG_$SEND_HW - Send message using hardware address routing
 *
 * Parameters:
 *   hw_addr_info    - Hardware address info (network at +6, socket at +8)
 *   dest_proc       - Pointer to destination process ID
 *   dest_node       - Pointer to destination node ID
 *   dest_sock       - Pointer to destination socket
 *   src_proc        - Pointer to source process ID
 *   src_node        - Pointer to source node ID
 *   src_sock        - Pointer to source socket
 *   msg_desc        - Message descriptor
 *   type_val        - Pointer to message type
 *   type_data       - Type data buffer
 *   header_len      - Pointer to header length
 *   data_ptr        - Data buffer pointer
 *   data_len        - Pointer to data length
 *   bytes_sent      - Pointer to bytes sent output
 *   status_ret      - Output: status code
 *
 * Original address: 0x00E59B14
 * Original size: 144 bytes
 */
void MSG_$SEND_HW(void *hw_addr_info,
                  uint32_t *dest_proc,
                  uint32_t *dest_node,
                  uint16_t *dest_sock,
                  uint32_t *src_proc,
                  uint32_t *src_node,
                  uint16_t *src_sock,
                  void *msg_desc,
                  uint16_t *type_val,
                  void *type_data,
                  uint16_t *header_len,
                  void *data_ptr,
                  uint16_t *data_len,
                  void *bytes_sent,
                  status_$t *status_ret)
{
#if defined(ARCH_M68K)
    int16_t port_num;
    uint16_t network;
    int16_t socket;
    uint8_t *addr_bytes = (uint8_t *)hw_addr_info;

    /* Extract network and socket from hardware address info */
    network = *(uint16_t *)(addr_bytes + 6);
    socket = *(int16_t *)(addr_bytes + 8);

    /* Look up the port for this network/socket combination */
    port_num = ROUTE_$FIND_PORT(network, (int32_t)socket);

    if (port_num == -1) {
        /* Port not found */
        *status_ret = status_$internet_unknown_network_port;
        return;
    }

    /* Call MSG_$$SEND with the found port */
    MSG_$$SEND(port_num,
               *dest_proc,
               *dest_node,
               *dest_sock,
               *src_proc,
               *src_node,
               *src_sock,
               msg_desc,
               *type_val,
               type_data,
               *header_len,
               data_ptr,
               *data_len,
               bytes_sent,
               status_ret);

#else
    (void)hw_addr_info;
    (void)dest_proc;
    (void)dest_node;
    (void)dest_sock;
    (void)src_proc;
    (void)src_node;
    (void)src_sock;
    (void)msg_desc;
    (void)type_val;
    (void)type_data;
    (void)header_len;
    (void)data_ptr;
    (void)data_len;
    (void)bytes_sent;
    *status_ret = status_$msg_socket_out_of_range;
#endif
}
