/*
 * MSG_$SEND, MSG_$SENDI, MSG_$$SEND - Send a message
 *
 * Sends a message to a specified destination.
 * Handles both local (same node) and remote (network) delivery.
 *
 * Original addresses:
 *   MSG_$SEND:  0x00E599FC (170 bytes) - wrapper
 *   MSG_$SENDI: 0x00E59AA6 (110 bytes) - internal wrapper
 *   MSG_$$SEND: 0x00E0D9EC (732 bytes) - implementation
 */

#include "msg/msg_internal.h"
#include "pkt/pkt.h"
#include "netbuf/netbuf.h"
#include "net_io/net_io.h"

/*
 * Network message header maximum size
 */
#define MSG_MAX_HEADER_SIZE     0x200

/*
 * MSG_$$SEND - Internal send implementation
 *
 * Parameters:
 *   port_num    - Port number (-1 for auto-select)
 *   dest_proc   - Destination process ID
 *   dest_node   - Destination node ID
 *   dest_sock   - Destination socket number
 *   src_proc    - Source process ID
 *   src_node    - Source node ID
 *   src_sock    - Source socket number
 *   msg_desc    - Message descriptor
 *   type_val    - Message type
 *   data_buf    - Data buffer pointer
 *   header_len  - Header length
 *   data_ptr    - Data pointer for network send
 *   data_len    - Data length
 *   result      - Result output
 *   status_ret  - Status return
 *
 * Original address: 0x00E0D9EC
 * Original size: 732 bytes
 */
status_$t MSG_$$SEND(int16_t port_num,
                      uint32_t dest_proc,
                      uint32_t dest_node,
                      int16_t dest_sock,
                      uint32_t src_proc,
                      uint32_t src_node,
                      int16_t src_sock,
                      void *msg_desc,
                      int16_t type_val,
                      void *data_buf,
                      uint16_t header_len,
                      void *data_ptr,
                      uint16_t data_len,
                      void *result,
                      status_$t *status_ret)
{
#if defined(ARCH_M68K)
    status_$t local_status = status_$ok;
    void *header_buf;
    uint32_t header_info;
    int16_t actual_port;

    /* Validate header size */
    if (header_len > MSG_MAX_HEADER_SIZE) {
        *status_ret = status_$network_message_header_too_big;
        return *status_ret;
    }

    /*
     * Copy message descriptor with flags set.
     * Set bit 2 (0x04) in byte at offset 1.
     */

    /* Get network header buffer */
    NETBUF_$GET_HDR(&header_buf, &header_info);

    /*
     * Build internet packet header.
     * PKT_$BLD_INTERNET_HDR handles address resolution and header formatting.
     */
    /* ... packet header building ... */

    /*
     * Determine port to use.
     * If port_num == -1, use auto-selected port.
     */
    actual_port = (port_num == -1) ? 0 : port_num;  /* Simplified */

    if (local_status != status_$ok) {
        NETBUF_$RTN_HDR(&header_info);
        *status_ret = local_status;
        return local_status;
    }

    /*
     * Check if destination is local (same node).
     */
    if (dest_node == *(uint32_t *)0xE245A4) {  /* NODE_$ME */
        /*
         * Local delivery - queue directly to socket.
         */
        if (data_len != 0) {
            /* Copy data to physical address for local delivery */
            /* PKT_$COPY_TO_PA(data_ptr, data_len, ...) */
        }

        /* Put message into socket queue */
        /* SOCK_$PUT(dest_sock, msg_info, ...) */

        *status_ret = status_$ok;
    } else {
        /*
         * Remote delivery - send over network.
         */

        /* Copy data if needed */
        if (data_len != 0) {
            /* Use data page or copy to packet buffer */
        }

        /* Lock network layer */
        ML_$LOCK(0x18);

        /* Send via network I/O */
        /* NET_IO_$SEND(port, header, data, ...) */

        /* Unlock network layer */
        ML_$UNLOCK(0x18);

        *status_ret = local_status;
    }

    /* Return header buffer */
    NETBUF_$RTN_HDR(&header_info);

    return *status_ret;

#else
    (void)port_num;
    (void)dest_proc;
    (void)dest_node;
    (void)dest_sock;
    (void)src_proc;
    (void)src_node;
    (void)src_sock;
    (void)msg_desc;
    (void)type_val;
    (void)data_buf;
    (void)header_len;
    (void)data_ptr;
    (void)data_len;
    (void)result;
    *status_ret = status_$msg_socket_out_of_range;
    return *status_ret;
#endif
}

/*
 * MSG_$SENDI - Send message internal wrapper
 *
 * Extracts parameters and calls MSG_$$SEND.
 */
void MSG_$SENDI(void *dest_proc,
                void *dest_node,
                int16_t *dest_sock,
                void *src_proc,
                void *src_node,
                int16_t *src_sock,
                void *data_buf,
                int16_t *type_val,
                void *type_data,
                int16_t *header_len,
                void *data_ptr,
                int16_t *data_len,
                int16_t *bytes_sent,
                status_$t *status_ret)
{
    int16_t result;

    MSG_$$SEND(-1,                           /* port_num = auto */
               *(uint32_t *)dest_proc,
               *(uint32_t *)dest_node,
               *dest_sock,
               *(uint32_t *)src_proc,
               *(uint32_t *)src_node,
               *src_sock,
               data_buf,                     /* msg_desc */
               *type_val,
               type_data,                    /* data_buf */
               *header_len,
               data_ptr,
               *data_len,
               &result,
               status_ret);

    *bytes_sent = result;
}

/*
 * MSG_$SEND - Send message wrapper
 *
 * Sets up message descriptor from MSG data base and calls MSG_$$SEND.
 */
void MSG_$SEND(void *dest_proc,
               int16_t *dest_node,
               int16_t *dest_sock,
               int16_t *src_sock,
               int16_t *type_val,
               void *data_buf,
               int16_t *header_len,
               void *data_ptr,
               int16_t *data_len,
               int16_t *bytes_sent,
               status_$t *status_ret)
{
#if defined(ARCH_M68K)
    uint8_t msg_desc[30];
    int i;
    int16_t result;

    /* Copy first 30 bytes from MSG data base for message descriptor */
    for (i = 0; i < 30; i++) {
        msg_desc[i] = *(uint8_t *)(MSG_$DATA_BASE + i);
    }

    /* Set source socket in descriptor */
    *(int16_t *)msg_desc = *src_sock;

    /* Call internal send with local node as source */
    MSG_$$SEND(-1,                           /* port_num = auto */
               *(uint32_t *)dest_proc,
               *(uint32_t *)0xE245A4,        /* NODE_$ME */
               *dest_sock,
               0,                            /* src_proc */
               *(uint32_t *)0xE245A4,        /* NODE_$ME */
               *dest_node,                   /* Actually src socket from desc */
               msg_desc,
               *type_val,
               data_buf,
               *header_len,
               data_ptr,
               *data_len,
               &result,
               status_ret);

    *bytes_sent = result;

#else
    (void)dest_proc;
    (void)dest_node;
    (void)dest_sock;
    (void)src_sock;
    (void)type_val;
    (void)data_buf;
    (void)header_len;
    (void)data_ptr;
    (void)data_len;
    *bytes_sent = 0;
    *status_ret = status_$msg_socket_out_of_range;
#endif
}
