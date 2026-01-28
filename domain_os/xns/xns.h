/*
 * XNS - Xerox Network Systems Internet Datagram Protocol (IDP)
 *
 * This module implements the XNS IDP protocol for Domain/OS. XNS was the
 * network protocol suite used by Apollo workstations before TCP/IP became
 * dominant. IDP is the unreliable datagram layer (similar to UDP in TCP/IP).
 *
 * The implementation supports up to 16 concurrent IDP channels, each with
 * its own port assignments and routing configuration.
 *
 * Key concepts:
 *   - Channels: Logical endpoints for IDP communication (0-15)
 *   - Ports: Network interface bindings for a channel (up to 8 ports per
 * channel)
 *   - Sockets: XNS socket numbers for demultiplexing (like UDP ports)
 *
 * Original location: 0xE2B314 (base of IDP state)
 */

#ifndef XNS_H
#define XNS_H

#include "base/base.h"
#include "ml/ml.h"

/*
 * XNS IDP packet type constants
 */
#define XNS_IDP_TYPE_ERROR 3 /* Error protocol packet */

/*
 * XNS reserved socket numbers
 */
#define XNS_SOCKET_RIP 1        /* Routing Information Protocol */
#define XNS_SOCKET_ECHO 2       /* Echo protocol */
#define XNS_SOCKET_ERROR 3      /* Error protocol */
#define XNS_SOCKET_ROUTER 0x499 /* Routing socket */

/*
 * Magic value indicating "no socket" or "invalid"
 */
#define XNS_NO_SOCKET 0xE1

/*
 * Maximum channels and ports
 */
#define XNS_MAX_CHANNELS 16
#define XNS_MAX_PORTS 8
#define XNS_MAX_ADDRS 4 /* Maximum registered addresses */

/*
 * First dynamic port number
 */
#define XNS_FIRST_DYNAMIC_PORT 0xBB9 /* 3001 decimal */

/*
 * IDP Header size (minimum packet size)
 */
#define XNS_IDP_HEADER_SIZE 30 /* 0x1E bytes */

/*
 * XNS Network Address (12 bytes)
 *
 * A complete XNS network address consists of:
 *   - Network number (4 bytes) - identifies the network segment
 *   - Host ID (6 bytes) - usually the Ethernet MAC address
 *   - Socket number (2 bytes) - identifies the application endpoint
 */
typedef struct xns_$net_addr_t {
  uint32_t network; /* 0x00: Network number */
  uint8_t host[6];  /* 0x04: Host ID (usually MAC address) */
  uint16_t socket;  /* 0x0A: Socket number */
} xns_$net_addr_t;

/*
 * XNS IDP Packet Header (30 bytes)
 *
 * This is the standard IDP header as defined by the XNS specification.
 * All multi-byte fields are in network byte order (big-endian).
 */
typedef struct xns_$idp_header_t {
  uint16_t checksum;     /* 0x00: Checksum (0xFFFF = none) */
  uint16_t length;       /* 0x02: Total packet length */
  uint8_t transport_ctl; /* 0x04: Transport control (hop count in low 4 bits) */
  uint8_t packet_type;   /* 0x05: Packet type */
  uint32_t dest_network; /* 0x06: Destination network */
  uint8_t dest_host[6];  /* 0x0A: Destination host */
  uint16_t dest_socket;  /* 0x10: Destination socket */
  uint32_t src_network;  /* 0x12: Source network */
  uint8_t src_host[6];   /* 0x16: Source host */
  uint16_t src_socket;   /* 0x1C: Source socket */
} xns_$idp_header_t;

/*
 * XNS IDP Channel State (0x48 = 72 bytes per channel)
 *
 * Each channel maintains state for an IDP endpoint.
 *
 * Layout:
 *   +0x00-0x3F: Per-port state (8 ports * 8 bytes each? or 12 bytes * 8?)
 *   +0x40-0x47: Port reference (0x40) and MAC socket (0x48)
 *   +0xA0: Demux callback function pointer
 *   +0xA4-0xBB: Connected destination address (12 bytes)
 *   +0xBC-0xD3: MAC layer info (24 bytes)
 *   +0xD4: Port index for connected mode
 *   +0xD6: User socket handle
 *   +0xD8: XNS socket number
 *   +0xDA: Flags and owning AS_ID
 *   +0xDC-0xE3: Per-port activation flags (8 bytes)
 *   +0xE4: Channel state flags (bit 7 = active)
 */
typedef struct xns_$channel_t {
  /* Per-port reference counts and MAC handles */
  uint8_t port_state[0x40]; /* 0x00: Port-specific state */
  uint32_t port_ref;        /* 0x40: Port reference */
  uint32_t port_info;       /* 0x44: Port info pointer */
  uint16_t mac_socket;      /* 0x48: MAC layer socket */
  uint16_t port_refcount;   /* 0x4A: Port reference count */

  uint8_t _unknown1[0x54]; /* 0x4C-0x9F: Unknown */

  void *demux_callback; /* 0xA0: Demultiplex callback function */

  /* Connected destination address (for connected mode) */
  uint32_t dest_network; /* 0xA4: Destination network */
  uint8_t dest_host[6];  /* 0xA8: Destination host */
  uint16_t dest_socket;  /* 0xAE: Destination socket (alias for dest_port) */
  uint32_t src_network;  /* 0xB0: Source network (for connected mode) */
  uint8_t src_host[6];   /* 0xB4: Source host */
  uint16_t src_port;     /* 0xBA: Source port */

  /* MAC layer information */
  uint8_t mac_info[0x18]; /* 0xBC-0xD3: MAC info for ARP etc. */

  int16_t connected_port; /* 0xD4: Port index for connected mode (-1 = any) */
  uint16_t user_socket;   /* 0xD6: User socket handle (0xE1 = none) */
  uint16_t xns_socket;    /* 0xD8: XNS socket number */
  uint16_t flags;         /* 0xDA: Flags and AS_ID (bits 5-10 = AS_ID) */

  /* Per-port activation bitmap */
  uint8_t
      port_active[8]; /* 0xDC-0xE3: Per-port active flags (bit 7 = active) */
  uint16_t state;     /* 0xE4: Channel state (bit 15 = active) */
  uint8_t _pad[2];    /* 0xE6-0xE7: Padding */
} xns_$channel_t;

/* Channel flags (in flags field at 0xDA) */
#define XNS_CHAN_FLAG_BIND_LOCAL 0x08   /* Bit 3: Bind to local address */
#define XNS_CHAN_FLAG_CONNECT 0x10      /* Bit 4: Connected mode */
#define XNS_CHAN_FLAG_BROADCAST 0x20    /* Bit 5: Broadcast capable */
#define XNS_CHAN_FLAG_AS_ID_MASK 0x07E0 /* Bits 5-10: Owning AS_ID */
#define XNS_CHAN_FLAG_AS_ID_SHIFT 5

/* Channel state flags */
#define XNS_CHAN_STATE_ACTIVE 0x8000 /* Bit 15: Channel is active */

/*
 * XNS IDP Global State
 *
 * This structure represents the complete IDP subsystem state at 0xE2B314.
 * It includes statistics, registered addresses, channel state, and the
 * exclusion lock for thread safety.
 */
typedef struct xns_$idp_state_t {
  /* Statistics (0x00-0x0B) */
  uint32_t packets_sent;     /* 0x00: Total packets sent */
  uint32_t packets_received; /* 0x04: Total packets received */
  uint32_t packets_dropped;  /* 0x08: Total packets dropped/errored */

  uint8_t _unknown1[0x04]; /* 0x0C-0x0F: Unknown */

  /* Registered network addresses (up to 8 ports * address) */
  int16_t port_network[8]; /* 0x10-0x1F: Port network IDs */

  /* Local network address info */
  uint16_t local_socket;  /* 0x20: Local socket (0x800) */
  uint16_t local_host_hi; /* 0x22: Local host ID high word */
  uint16_t local_host_lo; /* 0x24: Local host ID low word */

  /* Registered addresses array (up to 4) */
  uint16_t reg_addr[4][3]; /* 0x26-0x31: Registered address network IDs */
  uint16_t reg_host[4][3]; /* 0x32-0x4D: Registered host addresses */

  uint8_t _unknown2[0xCA]; /* 0x4E-0x313: Unknown/reserved */

  /* Channel array (16 channels, 0x48 bytes each) */
  xns_$channel_t channels[XNS_MAX_CHANNELS]; /* 0x314-0x7B3: actually relative
                                                to full state base */

  uint8_t _pad[0x10C]; /* Padding to reach 0x520 */

  /* Synchronization */
  ml_$exclusion_t lock; /* 0x520: Exclusion lock */

  /* Global counters */
  uint16_t open_channels;    /* 0x534: Number of open channels */
  uint16_t next_socket;      /* 0x536: Next dynamic socket number */
  uint16_t registered_count; /* 0x538: Number of registered addresses */
} xns_$idp_state_t;

/*
 * XNS IDP Statistics (returned by XNS_IDP_$GET_STATS)
 */
typedef struct xns_$idp_stats_t {
  uint32_t packets_sent;     /* Total packets sent */
  uint32_t packets_received; /* Total packets received */
  uint32_t packets_dropped;  /* Total packets dropped */
} xns_$idp_stats_t;

/*
 * XNS IDP Open Options
 *
 * Structure passed to XNS_IDP_$OPEN and XNS_IDP_$OS_OPEN.
 */
typedef struct xns_$idp_open_opt_t {
  int16_t version; /* 0x00: Version (must be 1) */
  int16_t socket;  /* 0x02: XNS socket number (0 = assign dynamically) */
  void *user_data; /* 0x04: User callback data */
  /* For connected mode: destination address */
  uint32_t dest_network;  /* 0x08: Destination network (0 = unconnected) */
  uint16_t dest_host_hi;  /* 0x0C: Destination host high word */
  uint16_t dest_host_mid; /* 0x0E: Destination host middle word */
  uint16_t dest_host_lo;  /* 0x10: Destination host low word */
  /* For local binding: source address */
  uint32_t src_network;  /* 0x14: Source network (0 = any) */
  uint16_t src_host_hi;  /* 0x18: Source host high word */
  uint16_t src_host_mid; /* 0x1A: Source host middle word */
  uint16_t src_host_lo;  /* 0x1C: Source host low word */
  int16_t channel_ret;   /* 0x1E: Returned: channel index (OS_OPEN) or unused */
  int16_t priority;      /* 0x20: Channel priority/index (OPEN) */
  uint8_t flags; /* 0x21: Open flags (bits 1,2,3 = bind/connect/noalloc) */
  uint8_t _pad;  /* 0x22: Padding */
  int16_t buffer_size; /* 0x22: Receive buffer size */
} xns_$idp_open_opt_t;

/* Open flags */
#define XNS_OPEN_FLAG_BIND_LOCAL 0x02 /* Bind to specific local port */
#define XNS_OPEN_FLAG_CONNECT 0x04    /* Connected mode */
#define XNS_OPEN_FLAG_NO_ALLOC                                                 \
  0x08 /* Don't allocate socket (OS internal use) */

/*
 * XNS IDP Send/Receive Buffer Descriptor
 *
 * Used for scatter-gather I/O operations.
 */
typedef struct xns_$idp_iov_t {
  int32_t length; /* 0x00: Buffer length (negative = error, 0 = end of list) */
  void *buffer;   /* 0x04: Buffer pointer */
  struct xns_$idp_iov_t *next; /* 0x08: Next descriptor in chain */
  uint8_t flags;               /* 0x0C: Flags */
} xns_$idp_iov_t;

/*
 * XNS IDP Send Parameters
 */
typedef struct xns_$idp_send_t {
  /* Destination address (24 bytes if unconnected) */
  uint8_t dest_addr[24]; /* 0x00: Destination address info */

  /* Packet info */
  int32_t header_len;  /* 0x18: Header length */
  void *header_ptr;    /* 0x1C: Header buffer pointer */
  xns_$idp_iov_t *iov; /* 0x20: I/O vector for data */
  uint8_t flags;       /* 0x24: Send flags */
  uint8_t _pad[7];     /* 0x25-0x2B: Padding */
  uint8_t packet_type; /* 0x2C: Packet type (or at +0x2D) */
  uint8_t _pad2;       /* 0x2D: Padding */
  /* Additional fields for checksum control etc. */
  uint8_t _extra[0x1A]; /* 0x2E-0x47: Extra fields */
} xns_$idp_send_t;

/*
 * Status codes for XNS IDP operations
 */
#define status_$xns_channel_table_full 0x3B0001   /* No free channels */
#define status_$xns_socket_already_open 0x3B0002  /* Socket already in use */
#define status_$xns_bad_channel 0x3B0004          /* Invalid channel number */
#define status_$xns_no_socket 0x3B0005            /* Channel has no socket */
#define status_$xns_no_data 0x3B0006              /* No data available */
#define status_$xns_buffer_too_small 0x3B0007     /* Receive buffer too small */
#define status_$xns_invalid_param 0x3B0008        /* Invalid parameter */
#define status_$xns_unknown_network_port 0x3B000B /* Unknown network port */
#define status_$xns_reserved_socket 0x3B000C    /* Socket number is reserved */
#define status_$xns_too_many_channels 0x3B000D  /* Channel limit exceeded */
#define status_$xns_socket_in_use 0x3B000E      /* Socket already in use */
#define status_$xns_no_route 0x3B0010           /* No route to destination */
#define status_$xns_bad_checksum 0x3B0011       /* Checksum error */
#define status_$xns_hop_count_exceeded 0x3B0012 /* Too many hops */
#define status_$xns_no_nexthop 0x3B0013         /* No next hop found */
#define status_$xns_version_mismatch 0x3B0015   /* Version mismatch */
#define status_$xns_packet_dropped 0x3B0016     /* Packet was dropped */
#define status_$xns_no_buffer_size 0x3B0017     /* Buffer size not specified */
#define status_$xns_incompatible_flags                                         \
  0x3B0018 /* Incompatible flags (bind+noalloc) */
#define status_$xns_incompatible_flags2                                        \
  0x3B0019 /* Incompatible flags (connect+noalloc) */
#define status_$xns_broadcast_no_addr 0x3B001A /* Broadcast requires address   \
                                                */
#define status_$xns_local_addr_in_use                                          \
  0x3B001B /* Local address already in use */
#define status_$xns_connect_bind_conflict                                      \
  0x3B001C                                  /* Connect and bind conflict */
#define status_$xns_too_many_addrs 0x3B001D /* Too many registered addresses   \
                                             */

/*
 * XNS Error Protocol codes (param to XNS_ERROR_$SEND)
 */
#define XNS_ERROR_UNSPEC 0x0000       /* Unspecified error */
#define XNS_ERROR_BAD_CHECKSUM 0x0001 /* Bad checksum */
#define XNS_ERROR_NO_SOCKET 0x0002    /* No socket listening */
#define XNS_ERROR_RESOURCE 0x0003     /* Resource exhausted */

/* Global reference to XNS IDP state (for M68K direct access) */
#if defined(ARCH_M68K)
#define XNS_$IDP_STATE ((xns_$idp_state_t *)0xE2B314)
#else
extern xns_$idp_state_t *XNS_$IDP_STATE;
#endif

/*
 * Public API Functions
 */

/*
 * XNS_IDP_$INIT - Initialize the XNS IDP subsystem
 *
 * Must be called during system startup before any XNS operations.
 * Initializes the channel table, exclusion lock, and default values.
 *
 * Original address: 0x00E30268
 */
void XNS_IDP_$INIT(void);

/*
 * XNS_IDP_$OPEN - Open an IDP channel (user-level)
 *
 * Opens a new IDP channel for user-mode communication.
 *
 * @param options       Pointer to open options structure
 * @param status_ret    Output: status code
 *
 * Original address: 0x00E187AC
 */
void XNS_IDP_$OPEN(xns_$idp_open_opt_t *options, status_$t *status_ret);

/*
 * XNS_IDP_$CLOSE - Close an IDP channel (user-level)
 *
 * Closes a previously opened IDP channel and releases resources.
 *
 * @param channel       Pointer to channel number
 * @param status_ret    Output: status code
 *
 * Original address: 0x00E189C4
 */
void XNS_IDP_$CLOSE(uint16_t *channel, status_$t *status_ret);

/*
 * XNS_IDP_$SEND - Send a packet (user-level)
 *
 * Sends an IDP packet through the specified channel.
 *
 * @param channel       Pointer to channel number
 * @param send_params   Send parameters structure
 * @param checksum_ret  Output: computed checksum (or 0)
 * @param status_ret    Output: status code
 *
 * Original address: 0x00E18A66
 */
void XNS_IDP_$SEND(uint16_t *channel, xns_$idp_send_t *send_params,
                   uint16_t *checksum_ret, status_$t *status_ret);

/*
 * XNS_IDP_$RECEIVE - Receive a packet (user-level)
 *
 * Receives an IDP packet from the specified channel.
 *
 * @param channel       Pointer to channel number
 * @param recv_params   Receive parameters/buffer structure
 * @param status_ret    Output: status code
 *
 * Original address: 0x00E18CE2
 */
void XNS_IDP_$RECEIVE(uint16_t *channel, void *recv_params,
                      status_$t *status_ret);

/*
 * XNS_IDP_$GET_STATS - Get IDP statistics
 *
 * Returns global IDP statistics counters.
 *
 * @param stats         Output: statistics structure
 * @param status_ret    Output: status code
 *
 * Original address: 0x00E18FD6
 */
void XNS_IDP_$GET_STATS(xns_$idp_stats_t *stats, status_$t *status_ret);

/*
 * XNS_IDP_$GET_PORT_INFO - Get port information
 *
 * Not implemented - always returns status_$mac_port_op_not_implemented.
 *
 * @param channel       Channel number
 * @param port_info     Output: port information
 * @param status_ret    Output: status code
 *
 * Original address: 0x00E18FB8
 */
void XNS_IDP_$GET_PORT_INFO(void *channel, void *port_info,
                            status_$t *status_ret);

/*
 * XNS_IDP_$REGISTER_ADDR - Register an additional network address
 *
 * Registers an additional XNS network address for this node.
 * Up to 4 addresses can be registered.
 *
 * @param addr          Address to register (6 bytes: network + host high)
 * @param port          Pointer to port number
 * @param status_ret    Output: status code
 *
 * Original address: 0x00E19002
 */
void XNS_IDP_$REGISTER_ADDR(uint16_t *addr, int16_t *port,
                            status_$t *status_ret);

/*
 * OS-Level Functions (kernel internal use)
 */

/*
 * XNS_IDP_$OS_OPEN - Open an IDP channel (OS-level)
 *
 * @param options       Pointer to open options
 * @param status_ret    Output: status code
 *
 * Original address: 0x00E17F02
 */
void XNS_IDP_$OS_OPEN(void *options, status_$t *status_ret);

/*
 * XNS_IDP_$OS_CLOSE - Close an IDP channel (OS-level)
 *
 * @param channel       Pointer to channel number
 * @param status_ret    Output: status code
 *
 * Original address: 0x00E181D8
 */
void XNS_IDP_$OS_CLOSE(int16_t *channel, status_$t *status_ret);

/*
 * XNS_IDP_$OS_SEND - Send a packet (OS-level)
 *
 * @param channel       Pointer to channel number
 * @param send_params   Send parameters
 * @param checksum_ret  Output: computed checksum
 * @param status_ret    Output: status code
 *
 * Original address: 0x00E18256
 */
void XNS_IDP_$OS_SEND(int16_t *channel, void *send_params,
                      uint16_t *checksum_ret, status_$t *status_ret);

/*
 * XNS_IDP_$OS_DEMUX - Demultiplex incoming packet (OS-level)
 *
 * Called by MAC layer when an IDP packet arrives.
 *
 * @param packet_info   Packet information structure
 * @param port          Pointer to port number
 * @param param3        Additional parameter
 * @param status_ret    Output: status code
 *
 * Original address: 0x00E184A8
 */
void XNS_IDP_$OS_DEMUX(void *packet_info, int16_t *port, void *param3,
                       status_$t *status_ret);

/*
 * XNS_IDP_$OS_ADD_PORT - Add a port to a channel (OS-level)
 *
 * @param channel       Pointer to channel number
 * @param port          Pointer to port number
 * @param status_ret    Output: status code
 *
 * Original address: 0x00E1872C
 */
void XNS_IDP_$OS_ADD_PORT(uint16_t *channel, uint16_t *port,
                          status_$t *status_ret);

/*
 * XNS_IDP_$OS_DELETE_PORT - Delete a port from a channel (OS-level)
 *
 * @param channel       Pointer to channel number
 * @param port          Pointer to port number
 * @param status_ret    Output: status code
 *
 * Original address: 0x00E1876C
 */
void XNS_IDP_$OS_DELETE_PORT(uint16_t *channel, uint16_t *port,
                             status_$t *status_ret);

/*
 * XNS_IDP_$DEMUX - Demultiplex incoming packet (user-level callback)
 *
 * Default demux callback for user channels.
 *
 * @param packet_info   Packet information
 * @param port_hi       Port high word pointer
 * @param port_lo       Port low word pointer
 * @param flags         Flags pointer
 * @param status_ret    Output: status code
 *
 * Original address: 0x00E18B8A
 */
void XNS_IDP_$DEMUX(void *packet_info, uint16_t *port_hi, uint16_t *port_lo,
                    char *flags, status_$t *status_ret);

/*
 * XNS_IDP_$PROC2_CLEANUP - Clean up channels for a terminating process
 *
 * Called when a process terminates to release its IDP channels.
 *
 * @param as_id         Address space ID of terminating process
 *
 * Original address: 0x00E18F0E
 */
void XNS_IDP_$PROC2_CLEANUP(uint16_t as_id);

/*
 * XNS_IDP_$CHECKSUM - Calculate IDP checksum
 *
 * Computes the XNS IDP checksum using one's complement addition
 * with end-around carry and rotation.
 *
 * @param data          Pointer to data (word-aligned)
 * @param word_count    Number of 16-bit words
 *
 * @return Checksum value (0 if result is 0xFFFF)
 *
 * Original address: 0x00E2B850
 */
uint16_t XNS_IDP_$CHECKSUM(uint16_t *data, int16_t word_count);

/*
 * XNS_IDP_$HOP_AND_SUM - Calculate hop count contribution to checksum
 *
 * Computes the checksum contribution from the hop count field,
 * used to update the checksum when forwarding a packet.
 *
 * @param current_sum   Current checksum value
 * @param hop_offset    Offset to hop count field (in bytes from header start)
 *
 * @return Updated checksum value
 *
 * Original address: 0x00E2B872
 */
int16_t XNS_IDP_$HOP_AND_SUM(uint16_t current_sum, int16_t hop_offset);

/*
 * XNS_ERROR_$SEND - Send an XNS Error Protocol packet
 *
 * Sends an error response packet for a received packet that could
 * not be processed.
 *
 * @param packet_info   Original packet information
 * @param error_code    Error code pointer
 * @param error_param   Error parameter pointer
 * @param result_ret    Output: result (unused)
 * @param status_ret    Output: status code
 *
 * Original address: 0x00E17A2E
 */
void XNS_ERROR_$SEND(void *packet_info, uint16_t *error_code,
                     uint16_t *error_param, uint16_t *result_ret,
                     status_$t *status_ret);

#endif /* XNS_H */
