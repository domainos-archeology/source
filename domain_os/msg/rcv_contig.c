/*
 * MSG_$RCV_CONTIG, MSG_$RCV_CONTIGI - Receive contiguous message
 *
 * Receives a message from a socket with the data placed into a contiguous
 * buffer. These functions handle extracting addressing information and
 * copying message data to user-provided buffers.
 *
 * Original addresses:
 *   MSG_$RCV_CONTIG:  0x00E59756 (80 bytes)
 *   MSG_$RCV_CONTIGI: 0x00E597A6 (426 bytes)
 *
 * The receive operation involves:
 * - Socket ownership validation
 * - Calling APP_$RECEIVE to dequeue and parse the network packet
 * - Extracting sender address information from the packet header
 * - Handling special address family processing (internet addresses)
 * - Copying message data to user buffer
 * - Managing network buffer resources
 */

#include "msg/msg_internal.h"
#include "app/app.h"
#include "pkt/pkt.h"
#include "netbuf/netbuf.h"

/*
 * APP_$RECEIVE result structure layout (44 bytes):
 *   +0x00: header_info (4 bytes) - local_3c[1] equivalent
 *   +0x04: data_ptr (4 bytes) - local_30
 *   +0x08: pkt_info_ptr (4 bytes) - local_34
 *   +0x0C: pkt_info[0..3] (16 bytes) - local_2c
 *   +0x1C: addr_info1 (4 bytes) - local_1c
 *   +0x20: addr_info2 (4 bytes) - local_18
 *   +0x24: flags (2 bytes) - local_c (bits 7-14 used)
 *   +0x26: reserved
 *
 * Packet info structure at local_34 pointer:
 *   +0x02: data_length (2 bytes)
 *   +0x04: overflow_length (2 bytes)
 *   +0x06: msg_type (2 bytes)
 *   +0x08: dest_proc (4 bytes)
 *   +0x0C: dest_sock (2 bytes)
 *   +0x0E: src_proc (4 bytes)
 *   +0x12: src_sock (2 bytes)
 *   +0x14: proto_family (1 byte)
 *   +0x15: proto_type (1 byte)
 *   +0x16: proto_subtype (1 byte)
 */

/* Internet address family constants */
#define ADDR_FAMILY_INTERNET    2
#define ADDR_SUBTYPE_INET       0x29
#define INET_ADDR_SIZE          16

/*
 * Hardware address info structure
 *
 * Extended address information returned by MSG_$RCV_CONTIGI and MSG_$RCV_HW.
 * Contains protocol family info and optional internet address.
 */
typedef struct msg_$hw_addr_s {
    uint16_t proto_family;      /* +0x00: Protocol family */
    uint16_t flags;             /* +0x02: Flags (from header bits 7-14) */
    uint16_t proto_type;        /* +0x04: Protocol type */
    uint16_t proto_subtype;     /* +0x06: Protocol subtype */
    uint16_t reserved1;         /* +0x08: Reserved */
    uint16_t reserved2;         /* +0x0A: 0 */
    uint16_t reserved3;         /* +0x0C: 0xFFFF */
    uint8_t  inet_addr[16];     /* +0x0E: Internet address (if applicable) */
} msg_$hw_addr_t;

/*
 * MSG_$RCV_CONTIGI - Internal receive contiguous implementation
 *
 * Parameters:
 *   socketidp   - Pointer to socket number
 *   dest_proc   - Output: destination process ID
 *   dest_node   - Output: destination node ID
 *   dest_sock   - Output: destination socket
 *   src_proc    - Output: source process ID
 *   src_node    - Output: source node ID
 *   src_sock    - Output: source socket
 *   hw_addr     - Output: hardware address info (30 bytes)
 *   msg_type    - Output: message type
 *   data_buf    - Buffer for message data
 *   max_len     - Pointer to maximum buffer length
 *   data_len    - Output: actual data length received
 *   status_ret  - Output: status code
 *
 * Original address: 0x00E597A6
 * Original size: 426 bytes
 */
void MSG_$RCV_CONTIGI(msg_$socket_t *socketidp,
                      uint32_t *dest_proc,
                      uint32_t *dest_node,
                      uint16_t *dest_sock,
                      uint32_t *src_proc,
                      uint32_t *src_node,
                      uint16_t *src_sock,
                      msg_$hw_addr_t *hw_addr,
                      uint16_t *msg_type,
                      char *data_buf,
                      uint16_t *max_len,
                      uint16_t *data_len,
                      status_$t *status_ret)
{
#if defined(M68K)
    int16_t sock_num;
    uint8_t asid;
    uint8_t byte_index;
    uint8_t *bitmap;

    /* Local variables matching Ghidra decompilation */
    uint32_t local_3c[2];       /* Header info for NETBUF_$RTN_HDR */
    int32_t local_34;          /* Packet info pointer */
    char *local_30;            /* Data pointer */
    int32_t local_2c[4];       /* Packet info copy */
    uint32_t local_1c;         /* addr_info1 */
    uint32_t local_18;         /* addr_info2 */
    uint16_t local_c;          /* Flags */

    uint8_t proto_type;
    uint8_t proto_subtype;
    uint16_t copy_len;
    uint16_t overflow_len;
    uint32_t available;

    sock_num = *socketidp;

    /* Validate socket number range (1-224) */
    if (sock_num < 1 || sock_num > MSG_MAX_SOCKET) {
        *status_ret = status_$msg_socket_out_of_range;
        return;
    }

    /* Check ownership bitmap */
    asid = PROC1_$AS_ID;
    bitmap = (uint8_t *)(MSG_$DATA_BASE + MSG_OFF_OWNERSHIP + (sock_num << 3));
    byte_index = (0x3F - asid) >> 3;

    if ((bitmap[byte_index] & (1 << (asid & 7))) == 0) {
        *status_ret = status_$msg_no_owner;
        return;
    }

    /* Call APP_$RECEIVE to get the message
     * This fills in our local variables via a packed structure
     */
    APP_$RECEIVE(*socketidp, &local_34, status_ret);

    if (*status_ret != status_$ok) {
        return;
    }

    /* Extract addressing information from receive result */
    *dest_proc = local_1c;
    *src_proc = local_18;
    *dest_node = *(uint32_t *)(local_34 + 8);
    *dest_sock = *(uint16_t *)(local_34 + 0xC);
    *src_node = *(uint32_t *)(local_34 + 0xE);
    *src_sock = *(uint16_t *)(local_34 + 0x12);
    *msg_type = *(uint16_t *)(local_34 + 6);

    /* Extract protocol info for hardware address structure */
    hw_addr->proto_family = *(uint8_t *)(local_34 + 0x14);
    hw_addr->flags = (local_c & 0x7F80) >> 7;
    proto_type = *(uint8_t *)(local_34 + 0x15);
    proto_subtype = *(uint8_t *)(local_34 + 0x16);
    hw_addr->proto_type = proto_type;
    hw_addr->proto_subtype = proto_subtype;
    hw_addr->reserved2 = 0;
    hw_addr->reserved3 = 0xFFFF;

    /* Handle internet address family (type 2, subtype 0x29)
     * Copy 16 bytes of internet address from data area
     */
    if (proto_type == ADDR_FAMILY_INTERNET && proto_subtype == ADDR_SUBTYPE_INET) {
        int i;
        char *addr_src = local_30;
        char *addr_dst = (char *)hw_addr->inet_addr;

        for (i = 0; i < INET_ADDR_SIZE; i++) {
            addr_dst[i] = addr_src[i];
        }

        /* Adjust data pointer and length past the address */
        *(int16_t *)(local_34 + 2) -= INET_ADDR_SIZE;
        local_30 += INET_ADDR_SIZE;
    }

    /* Determine how much data to copy */
    copy_len = *(uint16_t *)(local_34 + 2);
    if (*max_len < copy_len) {
        copy_len = *max_len;
    }
    *data_len = copy_len;

    /* Copy main data */
    OS_$DATA_COPY(local_30, data_buf, (uint32_t)copy_len);

    /* Handle overflow data if present */
    if (local_2c[0] != 0) {
        overflow_len = *(uint16_t *)(local_34 + 4);
        if (*max_len < overflow_len) {
            overflow_len = *max_len;
        }

        /* Calculate remaining buffer space */
        available = (uint32_t)*max_len - (uint32_t)copy_len;
        if ((int32_t)overflow_len < (int32_t)available) {
            available = overflow_len;
        }

        /* Copy overflow data */
        PKT_$DAT_COPY(local_2c, (int16_t)available, data_buf + *data_len);

        /* Release overflow buffers */
        PKT_$DUMP_DATA(local_2c, *(uint16_t *)(local_34 + 4));

        *data_len += (int16_t)available;
    }

    /* Return header buffer to pool
     * Align data pointer down to 1KB boundary for buffer return
     */
    local_3c[0] = (uint32_t)local_30 & ~0x3FF;
    NETBUF_$RTN_HDR(local_3c);

#else
    /* Non-M68K stub */
    (void)socketidp;
    (void)dest_proc;
    (void)dest_node;
    (void)dest_sock;
    (void)src_proc;
    (void)src_node;
    (void)src_sock;
    (void)hw_addr;
    (void)msg_type;
    (void)data_buf;
    (void)max_len;
    *data_len = 0;
    *status_ret = status_$msg_socket_out_of_range;
#endif
}

/*
 * MSG_$RCV_CONTIG - Receive contiguous message wrapper
 *
 * Simplified wrapper that calls MSG_$RCV_CONTIGI and extracts
 * only the commonly needed output parameters.
 *
 * Parameters:
 *   socketidp   - Pointer to socket number
 *   dest_node   - Output: destination node ID
 *   dest_sock   - Output: destination socket (via hw_addr_ret)
 *   hw_addr_ret - Output: first word of hardware address info
 *   src_node    - Output: source node ID
 *   src_node2   - Pointer to node info (dereferenced for src_node_ptr)
 *   msg_type    - Output: message type
 *   data_buf    - Buffer for message data
 *   status_ret  - Output: status code
 *
 * Original address: 0x00E59756
 * Original size: 80 bytes
 */
void MSG_$RCV_CONTIG(msg_$socket_t *socketidp,
                     uint32_t *dest_node,
                     uint32_t *dest_sock_out,
                     uint16_t *hw_addr_ret,
                     uint32_t *src_node,
                     uint32_t **src_node_ptr,
                     uint16_t *msg_type,
                     uint16_t *data_len,
                     status_$t *status_ret)
{
    /* Local storage for full internal call */
    uint32_t local_dest_proc;
    uint32_t local_dest_node;
    uint16_t local_dest_sock;
    uint32_t local_src_proc;
    uint32_t local_src_node;
    uint16_t local_src_sock;
    msg_$hw_addr_t local_hw_addr;
    uint16_t local_msg_type;

    MSG_$RCV_CONTIGI(socketidp,
                     &local_dest_proc,
                     &local_dest_node,
                     &local_dest_sock,
                     &local_src_proc,
                     &local_src_node,
                     &local_src_sock,
                     &local_hw_addr,
                     &local_msg_type,
                     (char *)*src_node_ptr,  /* data_buf passed via pointer */
                     msg_type,               /* max_len */
                     data_len,
                     status_ret);

    /* Copy selected outputs */
    *hw_addr_ret = local_hw_addr.proto_family;
}
