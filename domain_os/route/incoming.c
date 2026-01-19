/*
 * ROUTE_$INCOMING - Handle incoming routed packets
 *
 * This function processes packets received from user routing ports
 * that need to be injected into the local network. It validates
 * the packet format, copies data to network buffers, and queues
 * the packet for transmission.
 *
 * Packet format (from user):
 *   +0x00: Checksum/magic (4 bytes) - 0xDEC0DED or computed hash
 *   +0x04: Header data (header_len bytes from +0x14)
 *   +0x04+header_len: Data portion (data_len bytes from +0x18)
 *
 * Header fields at param_2 + 4:
 *   +0x10: header_len (uint16_t)
 *   +0x14: data_len (uint16_t)
 *
 * @param port_info     Port information structure (network at +6, socket at +8)
 * @param packet_data   Packet data buffer
 * @param length_ptr    Pointer to packet length
 * @param status_ret    Output: status code
 *
 * Status codes:
 *   status_$ok: Success
 *   status_$internet_unknown_network_port (0x2B0003): Port not found or wrong type
 *   status_$route_not_enabled (0x2B0001): Port not in routing mode
 *   status_$route_bad_packet_format (0x2B000D): Packet too small or malformed
 *   status_$route_checksum_error (0x2B000C): Packet checksum mismatch
 *
 * Original address: 0x00E878A8
 */

#include "route/route_internal.h"
#include "netbuf/netbuf.h"
#include "net_io/net_io.h"
#include "os/os.h"

/* Status codes */
#define status_$route_not_enabled       0x2B0001
#define status_$route_bad_packet_format 0x2B000D
#define status_$route_checksum_error    0x2B000C

/* Minimum packet size (header structure + checksum) */
#define MIN_PACKET_SIZE     0x1C

/* Magic value indicating no checksum validation */
#define CHECKSUM_MAGIC      0x0DEC0DED

/* Port state mask for routing-capable states (bits 0 and 1) */
#define PORT_STATE_ROUTING_MASK     0x03

/* Forward declarations */
void OS_$DATA_COPY(const void *src, void *dst, uint32_t length);
void NETBUF_$GET_DAT(void **buf_ret);
void NETBUF_$GETVA(void *buf, void **va_ret, status_$t *status);
void NETBUF_$RTNVA(void **va_ptr);
void NETBUF_$GET_HDR(void *info, void **hdr_ret);
void NET_IO_$PUT_IN_SOCK(uint16_t net_type, uint16_t socket, void **hdr_ptr,
                         void **data_ptr, uint16_t hdr_len, uint16_t data_len);

/*
 * Packet header structure (at param_2 + 4)
 *
 * This structure describes the format of data passed from user space
 * to be routed into the network.
 */
typedef struct incoming_header {
    uint8_t     pad[0x10];      /* 0x00-0x0F: Reserved/unknown */
    uint16_t    header_len;     /* 0x10: Length of header data */
    uint16_t    pad2;           /* 0x12: Padding */
    uint16_t    data_len;       /* 0x14: Length of payload data */
    /* Header data follows at +0x00 (start of this struct) */
} incoming_header_t;

void ROUTE_$INCOMING(void *port_info, uint8_t *packet_data, uint16_t *length_ptr,
                     status_$t *status_ret)
{
    int16_t port_index;
    route_$port_t *port;
    uint16_t port_network;
    int16_t port_socket;
    uint16_t packet_length;
    uint16_t header_len;
    uint16_t data_len;
    uint32_t total_needed;
    uint32_t checksum;
    uint32_t computed_checksum;
    uint8_t *header_start;
    void *data_buf;
    void *data_va;
    void *header_buf;
    uint8_t header_info[8];
    int16_t i;

    /*
     * Extract port identifiers from port_info structure
     */
    port_network = *(uint16_t *)((uint8_t *)port_info + 6);
    port_socket = *(int16_t *)((uint8_t *)port_info + 8);

    /*
     * Find the port by network/socket
     */
    port_index = ROUTE_$FIND_PORT(port_network, (int32_t)port_socket);

    /*
     * Validate port exists and is network type 2 (routing)
     */
    if (port_index == -1 || port_network != ROUTE_PORT_TYPE_ROUTING) {
        *status_ret = status_$internet_unknown_network_port;
        return;
    }

    /*
     * Get port structure and validate state
     * Port state must NOT have bits 0 or 1 set (states 0 or 1)
     * AND port type must be 2 (routing)
     */
    port = &ROUTE_$PORT_ARRAY[port_index];

    if (((1 << (port->active & 0x1f)) & PORT_STATE_ROUTING_MASK) != 0 ||
        port->port_type != ROUTE_PORT_TYPE_ROUTING) {
        *status_ret = status_$route_not_enabled;
        return;
    }

    /*
     * Validate minimum packet size
     */
    packet_length = *length_ptr;
    if (packet_length < MIN_PACKET_SIZE) {
        *status_ret = status_$route_bad_packet_format;
        return;
    }

    /*
     * Parse header structure
     * Header starts at packet_data + 4 (after checksum)
     */
    header_start = packet_data + 4;
    header_len = *(uint16_t *)(header_start + 0x10);
    data_len = *(uint16_t *)(header_start + 0x14);

    /*
     * Validate packet has enough data
     * Required: 4 (checksum) + header_len + data_len
     */
    total_needed = (uint32_t)header_len + (uint32_t)data_len + 4;
    if (total_needed > (uint32_t)packet_length) {
        *status_ret = status_$route_bad_packet_format;
        return;
    }

    /*
     * Validate checksum
     * First 4 bytes are either magic value (0xDEC0DED = no check)
     * or a computed hash of the packet data
     */
    OS_$DATA_COPY(packet_data, &checksum, 4);

    if (checksum != CHECKSUM_MAGIC) {
        /*
         * Compute checksum over packet data
         * Algorithm: hash = magic; for each byte: hash = byte + hash * 17
         */
        computed_checksum = CHECKSUM_MAGIC;

        for (i = packet_length - 5; i >= 0; i--) {
            computed_checksum = (uint32_t)packet_data[4 + (packet_length - 5 - i)] +
                               computed_checksum * 0x11;
        }

        if (computed_checksum != checksum) {
            *status_ret = status_$route_checksum_error;
            return;
        }
    }

    /*
     * Allocate data buffer if there's payload data
     */
    if (data_len == 0) {
        data_buf = NULL;
    } else {
        NETBUF_$GET_DAT(&data_buf);
        NETBUF_$GETVA(data_buf, &data_va, status_ret);

        if (*status_ret != status_$ok) {
            CRASH_SYSTEM(status_ret);
        }

        /* Copy data portion to network buffer */
        OS_$DATA_COPY(header_start + header_len, data_va, (uint32_t)data_len);

        NETBUF_$RTNVA(&data_va);
    }

    /*
     * Allocate header buffer and copy header data
     */
    NETBUF_$GET_HDR(header_info, &header_buf);
    OS_$DATA_COPY(header_start, header_buf, (uint32_t)header_len);

    /*
     * Inject packet into local network via socket
     * Network type 2 = routing port
     */
    NET_IO_$PUT_IN_SOCK(ROUTE_PORT_TYPE_ROUTING, port_socket,
                        &header_buf, &data_buf, header_len, data_len);

    *status_ret = status_$ok;
}
