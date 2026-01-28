/*
 * MSG_$RCV_HW - Receive message with hardware address information
 *
 * Receives a message from a socket and returns extended hardware
 * address information. This is similar to MSG_$RCV_CONTIGI but
 * uses a helper function (FUN_00e59548) for the main work.
 *
 * Original address: 0x00E59950 (172 bytes)
 *
 * The function validates socket ownership and then delegates
 * to an internal helper for the actual receive operation.
 */

#include "msg/msg_internal.h"
#include "app/app.h"
#include "pkt/pkt.h"
#include "netbuf/netbuf.h"

/*
 * Internal receive helper (FUN_00e59548)
 *
 * This is the shared implementation used by MSG_$RCV_HW and other
 * receive variants. It handles the actual receive operation after
 * socket validation.
 *
 * Original address: 0x00E59548
 * Original size: 362 bytes
 */
static void msg_$rcv_hw_internal(
    int16_t socket,
    uint32_t *dest_proc,
    uint32_t *dest_node,
    uint16_t *dest_sock,
    uint32_t *src_proc,
    uint32_t *src_node,
    uint16_t *src_sock,
    uint16_t *hw_addr,      /* 30-byte hardware address structure */
    uint16_t *msg_type,
    char *data_buf,
    uint16_t max_data_len,
    uint16_t *data_len,
    void *overflow_buf,
    uint16_t max_overflow_len,
    uint16_t *overflow_len,
    uint16_t *hw_extra1,
    uint16_t *hw_extra2,
    status_$t *status_ret)
{
#if defined(ARCH_M68K)
    /* Local variables matching Ghidra decompilation */
    uint32_t local_40[3];      /* Header info for NETBUF_$RTN_HDR */
    int32_t local_34;          /* Packet info pointer */
    char *local_30;            /* Data pointer */
    int32_t local_2c[4];       /* Packet info copy / overflow buffers */
    uint32_t local_1c;         /* addr_info1 */
    uint32_t local_18;         /* addr_info2 */
    uint16_t local_c;          /* Flags */

    uint8_t proto_type;
    uint8_t proto_subtype;
    uint16_t copy_len;
    uint32_t page_base;

    /* Call APP_$RECEIVE to get the message */
    APP_$RECEIVE(socket, &local_34, status_ret);

    if (*status_ret != status_$ok) {
        return;
    }

    /* Extract addressing information */
    *dest_proc = local_1c;
    *src_proc = local_18;
    *dest_node = *(uint32_t *)(local_34 + 8);
    *dest_sock = *(uint16_t *)(local_34 + 0xC);
    *src_node = *(uint32_t *)(local_34 + 0xE);
    *src_sock = *(uint16_t *)(local_34 + 0x12);
    *msg_type = *(uint16_t *)(local_34 + 6);

    /* Extract protocol info for hardware address structure */
    hw_addr[0] = *(uint8_t *)(local_34 + 0x14);           /* proto_family */
    hw_addr[1] = (local_c & 0x7F80) >> 7;                 /* flags */
    proto_type = *(uint8_t *)(local_34 + 0x15);
    proto_subtype = *(uint8_t *)(local_34 + 0x16);
    hw_addr[2] = proto_type;                              /* proto_type */
    hw_addr[3] = proto_subtype;                           /* proto_subtype */
    hw_addr[5] = 0;                                       /* reserved */
    hw_addr[6] = 0xFFFF;                                  /* marker */

    /* Handle internet address (type 2, subtype 0x29) */
    if (proto_type == 2 && proto_subtype == 0x29) {
        int i;
        char *addr_src = local_30;
        char *addr_dst = (char *)(hw_addr + 7);

        for (i = 0; i < 16; i++) {
            addr_dst[i] = addr_src[i];
        }

        /* Adjust past address */
        *(int16_t *)(local_34 + 2) -= 16;
        local_30 += 16;
    }

    /* Copy main data */
    copy_len = *(uint16_t *)(local_34 + 2);
    if (max_data_len < copy_len) {
        copy_len = max_data_len;
    }
    *data_len = copy_len;

    OS_$DATA_COPY(local_30, data_buf, (uint32_t)copy_len);

    /* Extract extra hardware info from page header */
    page_base = (uint32_t)local_30 & ~0x3FF;
    *hw_extra1 = *(uint16_t *)(page_base + 0x3E0);
    *hw_extra2 = *(uint16_t *)(page_base + 0x3E2);

    /* Handle overflow data */
    if (local_2c[0] == 0) {
        *overflow_len = 0;
    } else {
        uint16_t ovf_copy = *(uint16_t *)(local_34 + 4);
        if (max_overflow_len < ovf_copy) {
            ovf_copy = max_overflow_len;
        }
        *overflow_len = ovf_copy;

        PKT_$DAT_COPY(local_2c, ovf_copy, overflow_buf);
        PKT_$DUMP_DATA(local_2c, *(uint16_t *)(local_34 + 4));
    }

    /* Return header buffer */
    local_40[0] = page_base;
    NETBUF_$RTN_HDR(local_40);

#else
    (void)socket;
    (void)dest_proc;
    (void)dest_node;
    (void)dest_sock;
    (void)src_proc;
    (void)src_node;
    (void)src_sock;
    (void)hw_addr;
    (void)msg_type;
    (void)data_buf;
    (void)max_data_len;
    (void)overflow_buf;
    (void)max_overflow_len;
    *data_len = 0;
    *overflow_len = 0;
    *hw_extra1 = 0;
    *hw_extra2 = 0;
    *status_ret = status_$msg_socket_out_of_range;
#endif
}

/*
 * MSG_$RCV_HW - Receive message with hardware address information
 *
 * Validates socket ownership and delegates to internal helper.
 *
 * Parameters:
 *   socketidp       - Pointer to socket number
 *   dest_node       - Output: destination node ID
 *   dest_sock       - Output: destination socket
 *   hw_addr_ret     - Output: first word of hardware address
 *   src_node        - Output: source node ID
 *   src_sock        - Output: source socket
 *   msg_type_ptr    - Pointer to message type / max_type_len
 *   hw_addr_buf     - Output: hardware address buffer
 *   data_buf_ptr    - Pointer to data buffer
 *   src_node2       - Output: source node info
 *   max_data_len    - Pointer to max data length
 *   overflow_buf    - Output: overflow data buffer
 *   overflow_info   - Overflow buffer info
 *   max_overflow    - Pointer to max overflow length
 *   status_ret      - Output: status code
 *
 * Original address: 0x00E59950
 * Original size: 172 bytes
 */
void MSG_$RCV_HW(msg_$socket_t *socketidp,
                 uint32_t *dest_node,
                 uint32_t *dest_sock,
                 uint16_t *hw_addr_ret,
                 uint32_t *src_node,
                 uint32_t *src_sock,
                 uint16_t *msg_type_ptr,
                 uint16_t *hw_addr_buf,
                 void *data_buf_ptr,
                 uint32_t *src_node2,
                 uint16_t *max_data_len,
                 void *overflow_buf,
                 void *overflow_info,
                 uint16_t *max_overflow,
                 status_$t *status_ret)
{
#if defined(ARCH_M68K)
    int16_t sock_num;
    uint8_t asid;
    uint8_t byte_index;
    uint8_t *bitmap;

    /* Local storage for internal call */
    uint32_t local_dest_proc;
    uint32_t local_dest_node;
    uint16_t local_dest_sock;
    uint32_t local_src_proc;
    uint32_t local_src_node;
    uint16_t local_src_sock;
    uint16_t local_hw_addr[20];  /* 40 bytes for full hw addr */
    uint16_t local_msg_type;
    uint16_t local_data_len;
    uint16_t local_overflow_len;
    uint16_t local_hw_extra1;
    uint16_t local_hw_extra2;

    sock_num = *socketidp;

    /* Validate socket number range */
    if (sock_num < 1 || sock_num > MSG_MAX_SOCKET) {
        *status_ret = status_$msg_socket_out_of_range;
        goto done;
    }

    /* Check ownership bitmap */
    asid = PROC1_$AS_ID;
    bitmap = (uint8_t *)(MSG_$DATA_BASE + MSG_OFF_OWNERSHIP + (sock_num << 3));
    byte_index = (0x3F - asid) >> 3;

    if ((bitmap[byte_index] & (1 << (asid & 7))) == 0) {
        *status_ret = status_$msg_no_owner;
        goto done;
    }

    /* Call internal helper */
    msg_$rcv_hw_internal(
        *socketidp,
        &local_dest_proc,
        &local_dest_node,
        &local_dest_sock,
        &local_src_proc,
        &local_src_node,
        &local_src_sock,
        local_hw_addr,
        &local_msg_type,
        (char *)data_buf_ptr,
        *max_data_len,
        &local_data_len,
        overflow_buf,
        *max_overflow,
        &local_overflow_len,
        &local_hw_extra1,
        &local_hw_extra2,
        status_ret);

done:
    /* Copy hardware address first word to output */
    *hw_addr_ret = local_hw_addr[0];

#else
    (void)socketidp;
    (void)dest_node;
    (void)dest_sock;
    (void)src_node;
    (void)src_sock;
    (void)msg_type_ptr;
    (void)hw_addr_buf;
    (void)data_buf_ptr;
    (void)src_node2;
    (void)max_data_len;
    (void)overflow_buf;
    (void)overflow_info;
    (void)max_overflow;
    *hw_addr_ret = 0;
    *status_ret = status_$msg_socket_out_of_range;
#endif
}
