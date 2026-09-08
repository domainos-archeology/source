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
#include "mac_os/mac_os.h"
#include "route/route.h"   /* status_$internet_network_port_not_open (0x2B0001) */

/*
 * ============================================================================
 * Constants
 * ============================================================================
 */

/* Maximum number of network ports */
#define MAC_MAX_PORTS 8

/* Maximum number of channels per port */
#define MAC_MAX_CHANNELS 10

/* Maximum number of packet types per channel */
#define MAC_MAX_PACKET_TYPES 10

/* Special socket value indicating no socket allocated */
#define MAC_NO_SOCKET 0xE1

/*
 * ============================================================================
 * Status Codes (module 0x3A)
 * ============================================================================
 */
#define status_$mac_invalid_packet_type 0x3a0004
#define status_$mac_no_os_sockets_available 0x3a0006
#define status_$mac_channel_not_open 0x3a0008
#define status_$mac_no_socket_allocated 0x3a0009
#define status_$mac_no_packet_available_to_receive 0x3a000a
#define status_$mac_received_packet_too_big 0x3a000b
#define status_$mac_failed_to_put_packet_into_socket 0x3a0010
#define status_$mac_invalid_port 0x3a0011
#define status_$mac_invalid_packet_type_count 0x3a0012


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
  uint32_t min_type; /* Minimum type code (inclusive) */
  uint32_t max_type; /* Maximum type code (inclusive) */
} mac_$packet_type_t;

/*
 * MAC open parameters structure
 * Passed to MAC_$OPEN to specify channel configuration
 * Size: 0x54+ bytes
 */
typedef struct mac_$open_params_t {
  union {
    /* input: the packet-type ranges the caller wants routed to the channel */
    mac_$packet_type_t packet_types[MAC_MAX_PACKET_TYPES]; /* 0x00-0x4F */
    /*
     * output: MAC_$OPEN writes its three results over the FIRST entry, the
     * way a Pascal variant record would (0x00E0BA56-0x00E0BA60):
     *   move.l A0,(A2)          the registered EC2 event count
     *   move.l (-0x58,A6),(0x4,A2)  the MTU MAC_OS_$OPEN left in its local
     *   move.w D2w,(0x8,A2)     the channel number
     */
    struct {
      uint32_t ec2_handle;  /* 0x00: EC2_$REGISTER_EC1 result, as a target VA */
      uint32_t mtu;         /* 0x04: driver MTU */
      uint16_t channel_num; /* 0x08: channel number 0..9 */
    } result;
  } u;
  int16_t num_packet_types; /* 0x50: Number of packet types (1-10) */
  int16_t socket_count;     /* 0x52: Number of sockets to allocate */
  uint8_t flags;            /* 0x54: byte; bit 7 = promiscuous.  MAC_$OPEN
                             * reads it as a byte and shifts it down seven
                             * places ("move.b (0x54,A2),D1b / lsr.b #0x7,D1b"
                             * at 0x00E0BA2A). */
} mac_$open_params_t;

#if defined(ARCH_M68K)
_Static_assert(offsetof(mac_$open_params_t, u.result.ec2_handle) == 0x00,
               "mac_$open_params_t.result.ec2_handle");
_Static_assert(offsetof(mac_$open_params_t, u.result.mtu) == 0x04,
               "mac_$open_params_t.result.mtu");
_Static_assert(offsetof(mac_$open_params_t, u.result.channel_num) == 0x08,
               "mac_$open_params_t.result.channel_num");
_Static_assert(offsetof(mac_$open_params_t, num_packet_types) == 0x50,
               "mac_$open_params_t.num_packet_types");
_Static_assert(offsetof(mac_$open_params_t, socket_count) == 0x52,
               "mac_$open_params_t.socket_count");
_Static_assert(offsetof(mac_$open_params_t, flags) == 0x54,
               "mac_$open_params_t.flags");
#endif

/*
 * MAC channel handle structure
 * Returned by MAC_$OPEN, used for subsequent operations
 */
typedef struct mac_$channel_t {
  void *ec2_handle; /* 0x00: EC2 event count handle for receive notification */
  uint32_t os_handle;   /* 0x04: OS-level MAC handle */
  uint16_t channel_num; /* 0x08: Channel number (0-9) */
} mac_$channel_t;

/*
 * Buffer descriptor for receive operations
 * Used in linked list for scatter-gather receive
 */
typedef struct mac_$buffer_t {
  int32_t size; /* 0x00: Size of this buffer (negative = invalid) */
  void *data;   /* 0x04: Pointer to buffer data */
  struct mac_$buffer_t *next; /* 0x08: Next buffer in chain (NULL = end) */
} mac_$buffer_t;

/*
 * Transmit packet descriptor
 *
 * This is the same 0x4C-byte object MAC_OS_$SEND consumes: MAC_$SEND copies
 * the caller's record field by field into a local one and hands that to
 * MAC_OS_$SEND (0x00E0BBBA-0x00E0BC18).  The recovered layout, and the
 * assembly that justifies each field, live on mac_os_$send_pkt_t in
 * mac_os/mac_os.h; there is one definition so the two cannot drift.
 *
 * MAC_$SEND-specific notes:
 *   - is_broadcast (+0x18) is the caller's "please ARP this" request on the
 *     way in ("tst.b (0x18,A0) / bpl" at 0x00E0BB8C) and MAC_OS_$ARP's
 *     broadcast answer on the way out.
 *   - MAC_$SEND always clears hdr_prebuilt (+0x28) in its local copy
 *     (0x00E0BBF0), so MAC_OS_$SEND builds the header buffers itself.
 */
typedef mac_os_$send_pkt_t mac_$send_pkt_t;

/*
 * mac_$recv_pkt_t - the descriptor the caller of MAC_$RECEIVE supplies.
 *
 * Its head is the same shape as mac_os_$rcv_pkt_t: a mac_os_$link_addr_t at
 * 0x00, the "packet is local" boolean at 0x18, and the arrival time and frame
 * type at 0x2A/0x2E/0x30.  Where the driver record carries the payload length
 * at 0x1C, this one carries the FIRST buffer descriptor of the caller's
 * receive chain - MAC_$RECEIVE takes "lea (0x1c,A3),A2" (0x00E0BE9E) as the
 * head of a {length, address, next} list.
 *
 * MAC_$RECEIVE writes: 0x18 (0x00E0BE4E), 0x00 and 0x02.. (0x00E0BE52 and the
 * word loop at 0x00E0BE60), 0x2A (0x00E0BE6E), 0x2E (0x00E0BE74) and 0x30
 * (0x00E0BE7A).  It reads and rewrites the chain lengths from 0x1C onward.
 * Nothing beyond 0x33 is touched.
 */
typedef struct mac_$recv_pkt_t {
  mac_os_$link_addr_t link_addr;  /* 0x00: {word count, up to 11 words} */
  int8_t   is_local;              /* 0x18: Domain boolean, set from
                                   *       sock_$pkt_info_t.flags bit 0 */
  uint8_t  _pad_19[3];            /* 0x19 */
  mac_os_$buf_desc_t buffers;     /* 0x1C: head of the caller's buffer chain */
  uint8_t  _pad_28[2];            /* 0x28 */
  uint32_t time_high;             /* 0x2A: UNALIGNED longword */
  uint16_t time_low;              /* 0x2E */
  uint32_t frame_type;            /* 0x30 */
} __attribute__((packed)) mac_$recv_pkt_t;

#if defined(ARCH_M68K)
_Static_assert(offsetof(mac_$recv_pkt_t, is_local)   == 0x18, "mac_$recv_pkt_t.is_local");
_Static_assert(offsetof(mac_$recv_pkt_t, buffers)    == 0x1C, "mac_$recv_pkt_t.buffers");
_Static_assert(offsetof(mac_$recv_pkt_t, time_high)  == 0x2A, "mac_$recv_pkt_t.time_high");
_Static_assert(offsetof(mac_$recv_pkt_t, time_low)   == 0x2E, "mac_$recv_pkt_t.time_low");
_Static_assert(offsetof(mac_$recv_pkt_t, frame_type) == 0x30, "mac_$recv_pkt_t.frame_type");
_Static_assert(sizeof(mac_$recv_pkt_t) == 0x34, "mac_$recv_pkt_t must be 0x34 bytes");
#endif

/*
 * MAC channel table entry (internal)
 * Size: 20 bytes (0x14)
 * Base address: 0xE22990 + 0x7A8 = 0xE23138
 */
typedef struct mac_$channel_entry_t {
  uint16_t socket_num; /* 0x00 (offset 0x7A8): Socket number or 0xE1 */
  uint16_t port_num;   /* 0x02 (offset 0x7AA): Port number */
  uint16_t pad_04[4];  /* 0x04-0x0B: Unknown */
  uint16_t flags;      /* 0x0C (offset 0x7B2): Channel flags */
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

#if defined(ARCH_M68K)
/* Base address for MAC data */
#define MAC_$DATA_BASE 0xE22990

/* Channel table (10 entries of 20 bytes each) */
#define MAC_$CHANNEL_TABLE ((mac_$channel_entry_t *)(MAC_$DATA_BASE + 0x7A8))

/* ARP table */
#define MAC_$ARP_TABLE ((void *)(MAC_$DATA_BASE + 0x8E0))
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
 * On success the first three longwords of params are overwritten:
 *   - params->u.result.ec2_handle - event count for receive notification
 *   - params->u.result.mtu        - the port driver's MTU
 *   - params->u.result.channel_num - the channel that was allocated
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
void MAC_$OPEN(int16_t *port_num, mac_$open_params_t *params,
               status_$t *status_ret);

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
void MAC_$DEMUX(void *pkt_info, int16_t *port_num, int8_t *demux_flag,
                status_$t *status_ret);

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
void MAC_$RECEIVE(uint16_t *channel, mac_$recv_pkt_t *pkt_desc,
                  status_$t *status_ret);

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
