/*
 * MAC_OS - MAC Operating System Interface
 *
 * This module provides the low-level OS interface for the MAC (Media Access
 * Control) layer in Domain/OS. It handles the internal data structures and
 * operations that support the higher-level MAC_$ API.
 *
 * The MAC_OS layer manages:
 * - Channel state table (10 channels)
 * - Port packet type tables (8 ports, up to 20 entries each)
 * - Port version/info table
 * - Exclusion lock for thread safety
 *
 * Memory layout (m68k, base = 0xE22990):
 *   0x000-0x79F: Port packet type tables (8 ports * 0xF4 bytes)
 *   0x7A0-0x867: Channel state table (10 channels * 0x14 bytes)
 *   0x868-0x89B: Exclusion lock (ml_$exclusion_t)
 *   0x89C-0x8DB: Port info table (8 ports * 8 bytes)
 *
 * Original addresses:
 *   MAC_OS_$DATA:          0x00E22990
 *   MAC_OS_$CHANNEL_TABLE: 0x00E23130 (base + 0x7A0)
 *   MAC_OS_$EXCLUSION:     0x00E231F8 (base + 0x868)
 *   MAC_OS_$PORT_TABLE:    0x00E2322C (base + 0x89C)
 */

#ifndef MAC_OS_H
#define MAC_OS_H

#include "base/base.h"

/*
 * ============================================================================
 * Constants
 * ============================================================================
 */

/* Maximum number of network ports */
#define MAC_OS_MAX_PORTS        8

/* Maximum number of channels */
#define MAC_OS_MAX_CHANNELS     10

/* Maximum number of packet type entries per port */
#define MAC_OS_MAX_PKT_TYPES    20

/* Ethernet type for IP packets */
#define MAC_OS_ETHERTYPE_IP     0x0800

/* Special socket value indicating no socket allocated */
#define MAC_OS_NO_SOCKET        0xE1

/* Network type codes (from route_port_t offset +0x2E) */
#define MAC_OS_NET_TYPE_ETHERNET    0   /* Ethernet (802.3) */
#define MAC_OS_NET_TYPE_3           3   /* Unknown type 3 */
#define MAC_OS_NET_TYPE_TOKEN_RING  4   /* Token Ring */
#define MAC_OS_NET_TYPE_FDDI        5   /* FDDI */

/* Header sizes for different network types */
#define MAC_OS_HDR_SIZE_ETHERNET    0x1C
#define MAC_OS_HDR_SIZE_TOKEN_RING  0x0E
#define MAC_OS_HDR_SIZE_FDDI        0x00

/* Maximum packet size */
#define MAC_OS_MAX_PACKET_SIZE      0x7B8   /* 1976 bytes */
#define MAC_OS_SMALL_PACKET_SIZE    0x3B8   /* 952 bytes - fits in one buffer */
#define MAC_OS_LARGE_PACKET_SIZE    0x400   /* 1024 bytes */

/*
 * ============================================================================
 * Status Codes (module 0x3A)
 * ============================================================================
 */

#define status_$mac_port_op_not_implemented     0x3a0001
#define status_$mac_no_channels_available       0x3a0002
#define status_$mac_packet_type_table_full      0x3a0003
#define status_$mac_packet_type_in_use          0x3a0005
#define status_$mac_illegal_buffer_spec         0x3a000c
#define status_$mac_invalid_port_version        0x3a000d
#define status_$mac_XXX_unknown_2               0x3a000e
#define status_$mac_XXX_unknown                 0x3a000f
#define status_$mac_arp_address_not_found       0x3a0013

/* Cleanup handler set status (used by FIM) - defined in ec/ec.h as 0x00120035 */

/*
 * ============================================================================
 * Data Structures
 * ============================================================================
 */

/*
 * Packet type range entry (12 bytes)
 * Used to specify which Ethernet type codes should be routed to a channel.
 */
typedef struct mac_os_$pkt_type_entry_t {
    uint32_t    range_low;      /* 0x00: Minimum packet type (inclusive) */
    uint32_t    range_high;     /* 0x04: Maximum packet type (inclusive) */
    uint16_t    reserved;       /* 0x08: Reserved */
    uint16_t    channel_index;  /* 0x0A: Channel to route packets to */
} mac_os_$pkt_type_entry_t;

/*
 * Per-port packet type table (0xF4 = 244 bytes)
 * Each port maintains a table of packet type ranges and their target channels.
 */
typedef struct mac_os_$port_pkt_table_t {
    uint16_t    entry_count;    /* 0x00: Number of active entries */
    uint16_t    reserved;       /* 0x02: Padding */
    mac_os_$pkt_type_entry_t entries[MAC_OS_MAX_PKT_TYPES]; /* 0x04: Entries (20 * 12 = 240 bytes) */
} mac_os_$port_pkt_table_t;

/*
 * Channel state entry (0x14 = 20 bytes)
 * Tracks the state of each MAC_OS channel.
 */
typedef struct mac_os_$channel_t {
    void        *callback;      /* 0x00: Receive callback function pointer */
    void        *driver_info;   /* 0x04: Driver info structure pointer */
    uint16_t    socket;         /* 0x08: Socket number (0xE1 = no socket) */
    uint16_t    port_index;     /* 0x0A: Port number (0-7) */
    uint16_t    callback_data;  /* 0x0C: Saved callback data */
    uint16_t    line_number;    /* 0x0E: Line number */
    uint16_t    header_size;    /* 0x10: Header size for this network type */
    uint16_t    flags;          /* 0x12: Channel flags */
                                /*   Bit 9 (0x200): Channel in use */
                                /*   Bit 1 (0x002): Channel open */
                                /*   Bits 2-7: Owner AS_ID << 2 */
} mac_os_$channel_t;

/*
 * Port info entry (8 bytes)
 * Port version and configuration information.
 */
typedef struct mac_os_$port_info_t {
    uint32_t    version;        /* 0x00: Port version (must be 1) */
    uint32_t    config;         /* 0x04: Port configuration */
} mac_os_$port_info_t;

/*
 * MAC_OS open parameters structure
 * Passed to MAC_OS_$OPEN to configure channel.
 */
typedef struct mac_os_$open_params_t {
    uint32_t    mtu;            /* 0x00: Maximum transmission unit */
    uint16_t    unused_04;      /* 0x04: Unused */
    /* ... packet type entries ... */
    void        *callback;      /* 0x50: Callback function pointer */
    uint16_t    num_pkt_types;  /* 0x54: Number of packet type entries (offset 0x54 = 21 entries) */
} mac_os_$open_params_t;

/*
 * MAC_OS send packet descriptor
 * Describes packet to send.
 */
typedef struct mac_os_$send_pkt_t {
    uint8_t     pad_00[0x1c];   /* 0x00: Header fields */
    uint32_t    buffer_offset;  /* 0x1C: Offset into first buffer */
    uint32_t    header_ptr;     /* 0x20: Pointer to header data */
    uint32_t    header_next;    /* 0x24: Next buffer in chain */
    int8_t      needs_buffers;  /* 0x28: Negative = needs header buffer setup */
    uint8_t     pad_29[0xf];    /* 0x29: Padding */
    uint32_t    data_length;    /* 0x38: Data payload length */
    uint32_t    data_buffer;    /* 0x3C: Data buffer (set by send) */
} mac_os_$send_pkt_t;

/*
 * Driver info structure offsets
 * The driver_info pointer in route_port_t points to a structure with these:
 */
#define MAC_OS_DRIVER_MTU_OFFSET        0x04    /* MTU value */
#define MAC_OS_DRIVER_OPEN_OFFSET       0x3C    /* Open callback function */
#define MAC_OS_DRIVER_CLOSE_OFFSET      0x40    /* Close callback function */
#define MAC_OS_DRIVER_SEND_OFFSET       0x44    /* Send callback function */

/*
 * ============================================================================
 * Global Data
 * ============================================================================
 */

#if defined(M68K)
/* Base address for MAC_OS data structures */
#define MAC_OS_$DATA_BASE           0xE22990

/* Port packet type tables (8 ports * 0xF4 bytes) */
#define MAC_OS_$PORT_PKT_TABLES     ((mac_os_$port_pkt_table_t *)MAC_OS_$DATA_BASE)

/* Channel state table (10 channels * 0x14 bytes) */
#define MAC_OS_$CHANNEL_TABLE       ((mac_os_$channel_t *)(MAC_OS_$DATA_BASE + 0x7A0))

/* Exclusion lock */
#define MAC_OS_$EXCLUSION           ((void *)(MAC_OS_$DATA_BASE + 0x868))

/* Port info table (8 ports * 8 bytes) */
#define MAC_OS_$PORT_INFO_TABLE     ((mac_os_$port_info_t *)(MAC_OS_$DATA_BASE + 0x89C))

/* Route port pointer array */
#define ROUTE_$PORTP                (*(void ***)(0xE26EE8))

/* Node ME (local node address) */
#define NODE_$ME                    (*(uint32_t *)0xE245A4)
#else
/* Non-M68K: extern declarations */
extern mac_os_$port_pkt_table_t mac_os_$port_pkt_tables[MAC_OS_MAX_PORTS];
extern mac_os_$channel_t mac_os_$channel_table[MAC_OS_MAX_CHANNELS];
extern void *mac_os_$exclusion;
extern mac_os_$port_info_t mac_os_$port_info_table[MAC_OS_MAX_PORTS];
extern void **route_$portp[MAC_OS_MAX_PORTS];
extern uint32_t node_$me;
#endif

/*
 * ============================================================================
 * Function Prototypes
 * ============================================================================
 */

/*
 * MAC_OS_$INIT - Initialize the MAC_OS subsystem
 *
 * Initializes the exclusion lock, clears all packet type tables,
 * initializes channel table, and sets up port info entries.
 * Must be called during system startup.
 *
 * Original address: 0x00E2F4FC
 */
void MAC_OS_$INIT(void);

/*
 * MAC_OS_$OPEN - Open a MAC channel at OS level
 *
 * Opens a low-level MAC channel. Called by the higher-level MAC_$OPEN.
 *
 * Parameters:
 *   port_num   - Pointer to port number (0-7)
 *   params     - Open parameters (packet types, callback, etc.)
 *   status_ret - Pointer to receive status
 *
 * On success, updates params with:
 *   - MTU value from driver
 *   - Channel number
 *
 * Status codes:
 *   status_$ok - Success
 *   status_$mac_port_op_not_implemented - Port/driver not configured
 *   status_$mac_no_channels_available - All 10 channels in use
 *   status_$mac_packet_type_table_full - Port's packet table full
 *   status_$mac_packet_type_in_use - Packet type already registered
 *
 * Original address: 0x00E0B246
 */
void MAC_OS_$OPEN(int16_t *port_num, mac_os_$open_params_t *params, status_$t *status_ret);

/*
 * MAC_OS_$CLOSE - Close a MAC channel at OS level
 *
 * Closes a low-level MAC channel. Removes packet type entries,
 * calls driver close, and releases channel.
 *
 * Parameters:
 *   channel    - Pointer to channel number (0-9)
 *   status_ret - Pointer to receive status
 *
 * Status codes:
 *   status_$ok - Success
 *   status_$mac_port_op_not_implemented - Driver close not implemented
 *
 * Original address: 0x00E0B45C
 */
void MAC_OS_$CLOSE(int16_t *channel, status_$t *status_ret);

/*
 * MAC_OS_$SEND - Send a packet at OS level
 *
 * Prepares and sends a packet through the driver. Sets up network
 * buffers for the header and data portions.
 *
 * Parameters:
 *   channel    - Pointer to channel number
 *   pkt_desc   - Packet descriptor
 *   bytes_sent - Pointer to receive bytes sent
 *   status_ret - Pointer to receive status
 *
 * Status codes:
 *   status_$ok - Success
 *   status_$mac_port_op_not_implemented - Driver send not implemented
 *   status_$mac_illegal_buffer_spec - Invalid buffer specification
 *
 * Original address: 0x00E0B5A8
 */
void MAC_OS_$SEND(int16_t *channel, mac_os_$send_pkt_t *pkt_desc,
                  int16_t *bytes_sent, status_$t *status_ret);

/*
 * MAC_OS_$DEMUX - Demultiplex a received packet
 *
 * Routes an incoming packet to the appropriate channel callback
 * based on the packet type and port's packet type table.
 *
 * Parameters:
 *   pkt_info   - Received packet info
 *   port_num   - Pointer to port number
 *   param3     - Additional parameter (passed to callback)
 *   status_ret - Pointer to receive status
 *
 * Status codes:
 *   status_$ok - Success
 *   status_$mac_XXX_unknown - No matching channel found
 *
 * Original address: 0x00E0B816
 */
void MAC_OS_$DEMUX(void *pkt_info, int16_t *port_num, void *param3, status_$t *status_ret);

/*
 * MAC_OS_$PROC2_CLEANUP - Process cleanup for MAC_OS
 *
 * Called during process termination to clean up any MAC channels
 * owned by the terminating process.
 *
 * Parameters:
 *   as_id - Address space ID of terminating process
 *
 * Original address: 0x00E0BFDE
 */
void MAC_OS_$PROC2_CLEANUP(uint16_t as_id);

/*
 * MAC_OS_$ARP - Resolve address using ARP
 *
 * Resolves a network address to a MAC address. Handles broadcast
 * addresses specially.
 *
 * Parameters:
 *   addr_info  - Address information structure
 *   port_num   - Port number
 *   mac_addr   - Pointer to receive MAC address
 *   flags      - Pointer to receive flags (0xFF for broadcast)
 *   status_ret - Pointer to receive status
 *
 * Status codes:
 *   status_$ok - Success
 *   status_$mac_port_op_not_implemented - Invalid network type
 *   status_$mac_arp_address_not_found - Address not found
 *
 * Original address: 0x00E0C0CE
 */
void MAC_OS_$ARP(void *addr_info, int16_t port_num, uint16_t *mac_addr,
                 uint8_t *flags, status_$t *status_ret);

/*
 * MAC_OS_$PUT_INFO - Store port information
 *
 * Stores port version/configuration information after validating
 * that the network parameters don't conflict with existing ports.
 *
 * Parameters:
 *   info       - Port info to store
 *   port_num   - Pointer to port number
 *   status_ret - Pointer to receive status
 *
 * Status codes:
 *   status_$ok - Success
 *   status_$mac_invalid_port_version - Version != 1
 *   status_$mac_XXX_unknown_2 - Duplicate network configuration
 *
 * Original address: 0x00E0C228
 */
void MAC_OS_$PUT_INFO(mac_os_$port_info_t *info, int16_t *port_num, status_$t *status_ret);

#endif /* MAC_OS_H */
