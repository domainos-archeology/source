/*
 * MAC - Media Access Control Module
 *
 * This module provides the low-level network interface for Domain/OS.
 * It sits between the protocol layers (IP, etc.) and the network hardware
 * drivers, handling:
 *
 * - Opening/closing network channels
 * - Sending packets to the network
 * - Receiving packets from the network (via demux callback)
 * - ARP (Address Resolution Protocol) for address mapping
 * - Network port to number translation
 *
 * The MAC layer supports up to 8 network ports (0-7) and up to 10
 * simultaneous channels per port.
 *
 * Memory layout (m68k):
 *   - MAC channel table: 0xE23138 (offset from base 0xE22990 + 0x7A8)
 *   - Each channel entry is 20 bytes (0x14)
 *   - MAC exclusion lock: 0xE231F8 (base + 0x868)
 *   - ARP table: 0xE23270 (base + 0x8E0)
 */

#ifndef MAC_H
#define MAC_H

#include "base/base.h"

/*
 * ============================================================================
 * Constants
 * ============================================================================
 */

/* Maximum number of network ports */
#define MAC_MAX_PORTS           8

/* Maximum number of channels per port */
#define MAC_MAX_CHANNELS        10

/* Maximum number of packet types per channel */
#define MAC_MAX_PACKET_TYPES    10

/* Special socket value indicating no socket allocated */
#define MAC_NO_SOCKET           0xE1

/*
 * ============================================================================
 * Status Codes (module 0x3A)
 * ============================================================================
 */
#define status_$mac_invalid_packet_type         0x3a0004
#define status_$mac_no_os_sockets_available     0x3a0006
#define status_$mac_channel_not_open            0x3a0008
#define status_$mac_no_socket_allocated         0x3a0009
#define status_$mac_no_packet_available_to_receive 0x3a000a
#define status_$mac_received_packet_too_big     0x3a000b
#define status_$mac_illegal_buffer_spec         0x3a000c
#define status_$mac_XXX_unknown                 0x3a000f
#define status_$mac_failed_to_put_packet_into_socket 0x3a0010
#define status_$mac_invalid_port                0x3a0011
#define status_$mac_invalid_packet_type_count   0x3a0012

/* Internet status code used by MAC */
#define status_$internet_network_port_not_open  0x2b0001

/*
 * ============================================================================
 * Data Structures
 * ============================================================================
 */

/*
 * Packet type filter entry
 * Specifies a range of Ethernet type codes to accept
 */
typedef struct mac_$packet_type_t {
    uint32_t    min_type;       /* Minimum type code (inclusive) */
    uint32_t    max_type;       /* Maximum type code (inclusive) */
} mac_$packet_type_t;

/*
 * MAC open parameters structure
 * Passed to MAC_$OPEN to specify channel configuration
 * Size: 0x54+ bytes
 */
typedef struct mac_$open_params_t {
    mac_$packet_type_t packet_types[MAC_MAX_PACKET_TYPES]; /* 0x00-0x4F: Packet type filters */
    int16_t     num_packet_types;   /* 0x50: Number of packet types (1-10) */
    int16_t     socket_count;       /* 0x52: Number of sockets to allocate */
    uint8_t     flags;              /* 0x54: Flags (bit 7: promiscuous mode) */
} mac_$open_params_t;

/*
 * MAC channel handle structure
 * Returned by MAC_$OPEN, used for subsequent operations
 */
typedef struct mac_$channel_t {
    void        *ec2_handle;    /* 0x00: EC2 event count handle for receive notification */
    uint32_t    os_handle;      /* 0x04: OS-level MAC handle */
    uint16_t    channel_num;    /* 0x08: Channel number (0-9) */
} mac_$channel_t;

/*
 * Buffer descriptor for receive operations
 * Used in linked list for scatter-gather receive
 */
typedef struct mac_$buffer_t {
    int32_t     size;           /* 0x00: Size of this buffer (negative = invalid) */
    void        *data;          /* 0x04: Pointer to buffer data */
    struct mac_$buffer_t *next; /* 0x08: Next buffer in chain (NULL = end) */
} mac_$buffer_t;

/*
 * Transmit packet descriptor
 * Describes a packet to send
 */
typedef struct mac_$send_pkt_t {
    uint8_t     dest_addr[6];   /* 0x00: Destination MAC address */
    uint8_t     pad_06[2];      /* 0x06: Padding */
    uint8_t     src_addr[6];    /* 0x08: Source MAC address */
    uint8_t     pad_0e[2];      /* 0x0E: Padding */
    uint8_t     pad_10[4];      /* 0x10: Unknown */
    uint32_t    type_length;    /* 0x14: Ethernet type/length field */
    int8_t      arp_flag;       /* 0x18: If negative, ARP lookup needed */
    uint8_t     pad_19[3];      /* 0x19: Padding */
    uint32_t    header_data;    /* 0x1C: Header data pointer */
    uint32_t    header_size;    /* 0x20: Header size */
    uint32_t    body_chain;     /* 0x24: Pointer to body buffer chain */
    uint8_t     pad_28[8];      /* 0x28: Unknown */
    uint32_t    total_length;   /* 0x30: Total packet length */
    uint8_t     pad_34[8];      /* 0x34: Unknown */
    /* Additional fields at 0x3A+ for receive path */
} mac_$send_pkt_t;

/*
 * Receive packet descriptor
 * Describes a received packet
 */
typedef struct mac_$recv_pkt_t {
    int16_t     num_packet_types; /* 0x00: Number of packet type entries */
    uint16_t    packet_types[MAC_MAX_PACKET_TYPES]; /* 0x02: Packet type values */
    uint8_t     pad_16[4];      /* 0x16: Unknown */
    int8_t      arp_flag;       /* 0x1A: Broadcast flag (negative = multicast/broadcast) */
    uint8_t     pad_1b[3];      /* 0x1B: Padding */
    int16_t     field_1e;       /* 0x1E: Unknown */
    uint32_t    field_20;       /* 0x20: Pointer (header info?) */
    uint8_t     pad_24[6];      /* 0x24: Unknown */
    uint32_t    field_2a;       /* 0x2A: Unknown */
    int16_t     field_2e;       /* 0x2E: Unknown */
    uint32_t    field_30;       /* 0x30: Unknown */
    void        *channel_ptr;   /* 0x34: Pointer to channel info */
    uint8_t     pad_38[2];      /* 0x38: Unknown */
    int16_t     field_3a;       /* 0x3A: Unknown */
    uint32_t    field_3c;       /* 0x3C: Unknown */
    uint32_t    field_40;       /* 0x40: Unknown */
    uint32_t    field_44;       /* 0x44: Unknown */
    uint32_t    field_48;       /* 0x48: Unknown */
    mac_$buffer_t *buffers;     /* 0x1C: Receive buffers (in recv path) */
} mac_$recv_pkt_t;

/*
 * MAC channel table entry (internal)
 * Size: 20 bytes (0x14)
 * Base address: 0xE22990 + 0x7A8 = 0xE23138
 */
typedef struct mac_$channel_entry_t {
    uint16_t    socket_num;     /* 0x00 (offset 0x7A8): Socket number or 0xE1 */
    uint16_t    port_num;       /* 0x02 (offset 0x7AA): Port number */
    uint16_t    pad_04[4];      /* 0x04-0x0B: Unknown */
    uint16_t    flags;          /* 0x0C (offset 0x7B2): Channel flags */
                                /*   Bit 9 (0x200): Channel open */
                                /*   Bit 8 (0x100): Shared access */
                                /*   Bits 2-7: Owner ASID << 2 */
                                /*   Bit 0: Promiscuous mode */
} mac_$channel_entry_t;

/*
 * ============================================================================
 * Global Data
 * ============================================================================
 */

#if defined(M68K)
/* Base address for MAC data */
#define MAC_$DATA_BASE          0xE22990

/* Channel table (10 entries of 20 bytes each) */
#define MAC_$CHANNEL_TABLE      ((mac_$channel_entry_t *)(MAC_$DATA_BASE + 0x7A8))

/* ARP table */
#define MAC_$ARP_TABLE          ((void *)(MAC_$DATA_BASE + 0x8E0))
#else
extern mac_$channel_entry_t mac_$channel_table[MAC_MAX_CHANNELS];
extern void *mac_$arp_table;
#endif

/*
 * ============================================================================
 * Function Prototypes
 * ============================================================================
 */

/*
 * MAC_$OPEN - Open a MAC channel
 *
 * Opens a network channel on the specified port with the given packet
 * type filters. Returns a channel handle for use with send/receive.
 *
 * Parameters:
 *   port_num   - Pointer to network port number (0-7)
 *   params     - Channel configuration parameters
 *   status_ret - Pointer to receive status code
 *
 * On success:
 *   - params->ec2_handle contains event count for receive notification
 *   - params->os_handle contains OS-level handle
 *   - params->channel_num contains allocated channel number
 *
 * Status codes:
 *   status_$ok - Success
 *   status_$mac_invalid_port - Port number out of range
 *   status_$internet_network_port_not_open - Port not initialized
 *   status_$mac_invalid_packet_type_count - Invalid packet type count
 *   status_$mac_invalid_packet_type - min > max in a packet type range
 *   status_$mac_no_socket_allocated - Socket count is 0
 *   status_$mac_no_os_sockets_available - Cannot allocate socket
 *
 * Original address: 0x00E0B8BE
 */
void MAC_$OPEN(int16_t *port_num, mac_$open_params_t *params, status_$t *status_ret);

/*
 * MAC_$CLOSE - Close a MAC channel
 *
 * Closes a previously opened MAC channel and releases resources.
 *
 * Parameters:
 *   channel    - Pointer to channel number (from open)
 *   status_ret - Pointer to receive status code
 *
 * Status codes:
 *   status_$ok - Success
 *   status_$mac_channel_not_open - Channel not open or not owned by caller
 *
 * Original address: 0x00E0BA6C
 */
void MAC_$CLOSE(uint16_t *channel, status_$t *status_ret);

/*
 * MAC_$SEND - Send a packet
 *
 * Sends a packet on the specified channel. May perform ARP lookup
 * if the destination address is not cached.
 *
 * Parameters:
 *   channel    - Pointer to channel number
 *   pkt_desc   - Packet descriptor
 *   bytes_sent - Output: number of bytes sent
 *   status_ret - Pointer to receive status code
 *
 * Status codes:
 *   status_$ok - Success
 *   status_$mac_channel_not_open - Channel not open or not owned
 *
 * Original address: 0x00E0BB12
 */
void MAC_$SEND(uint16_t *channel, mac_$send_pkt_t *pkt_desc,
               uint16_t *bytes_sent, status_$t *status_ret);

/*
 * MAC_$DEMUX - Demultiplex received packet (callback)
 *
 * Internal callback function registered with the socket layer.
 * Routes incoming packets to the appropriate channel based on
 * packet type filters.
 *
 * Parameters:
 *   pkt_info   - Received packet information
 *   port_info  - Port information
 *   flags      - Receive flags
 *   status_ret - Pointer to receive status code
 *
 * Original address: 0x00E0BC4E
 */
void MAC_$DEMUX(void *pkt_info, int16_t *port_info, char *flags, status_$t *status_ret);

/*
 * MAC_$RECEIVE - Receive a packet
 *
 * Receives the next packet from the channel's socket queue.
 * Copies packet data into the provided buffer chain.
 *
 * Parameters:
 *   channel    - Pointer to channel number
 *   pkt_desc   - Packet descriptor (filled in on receive)
 *   status_ret - Pointer to receive status code
 *
 * The pkt_desc should have the buffers field (offset 0x1C) pointing
 * to a chain of mac_$buffer_t entries describing where to place data.
 *
 * Status codes:
 *   status_$ok - Success
 *   status_$mac_channel_not_open - Channel not open or not owned
 *   status_$mac_no_socket_allocated - No socket for channel
 *   status_$mac_no_packet_available_to_receive - No packet in queue
 *   status_$mac_received_packet_too_big - Buffers too small
 *   status_$mac_illegal_buffer_spec - Invalid buffer descriptor
 *
 * Original address: 0x00E0BDB0
 */
void MAC_$RECEIVE(uint16_t *channel, mac_$recv_pkt_t *pkt_desc, status_$t *status_ret);

/*
 * MAC_$NET_TO_PORT_NUM - Convert network ID to port number
 *
 * Looks up the port number for a given network identifier.
 *
 * Parameters:
 *   net_id     - Pointer to network identifier
 *   port_ret   - Pointer to receive port number (0-7, or -1 if not found)
 *
 * If net_id is 0, returns port 0.
 * Otherwise searches through the port table for a match.
 *
 * Original address: 0x00E0C350
 */
void MAC_$NET_TO_PORT_NUM(int32_t *net_id, int16_t *port_ret);

#endif /* MAC_H */
