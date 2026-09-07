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
 * Passed to MAC_OS_$OPEN to configure a channel.
 *
 * The packet-type filter array starts at offset 0x00: the registration loop
 * at 0x00E0B304 walks the record with "movea.l A3,A2 / addq.l #0x8,A3" and
 * copies two longwords per entry.  MAC_OS_$OPEN then overwrites that first
 * entry with its two results - the MTU longword at +0x00 ("move.l D1,(A2)"
 * at 0x00E0B42A) and the channel word at +0x04 ("move.w D2w,(0x4,A2)" at
 * 0x00E0B41C) - so the two views share storage exactly as a Pascal variant
 * record would.  The callback sits at +0x50 ("move.l (0x50,A1),(0x7a0,A0)"
 * at 0x00E0B366) and the entry count at +0x54 ("move.w (0x54,A3),D0w" at
 * 0x00E0B2F8).
 */
#define MAC_OS_MAX_OPEN_PKT_TYPES   10      /* 0x50 bytes of 8-byte entries */

typedef struct mac_os_$pkt_type_range_t {
    uint32_t    range_low;      /* 0x00 */
    uint32_t    range_high;     /* 0x04 */
} mac_os_$pkt_type_range_t;

typedef struct mac_os_$open_params_t {
    union {
        /* input: the packet-type ranges to register */
        mac_os_$pkt_type_range_t pkt_types[MAC_OS_MAX_OPEN_PKT_TYPES];
        /* output: written over entry 0 once the channel is open */
        struct {
            uint32_t    mtu;        /* 0x00: driver MTU */
            uint16_t    channel;    /* 0x04: channel number 0..9 */
        } result;
    } u;
    void        *callback;      /* 0x50: demux callback */
    uint16_t    num_pkt_types;  /* 0x54: entries supplied in u.pkt_types */
} mac_os_$open_params_t;

#if defined(ARCH_M68K)
_Static_assert(offsetof(mac_os_$open_params_t, callback) == 0x50,
               "mac_os_$open_params_t.callback");
_Static_assert(offsetof(mac_os_$open_params_t, num_pkt_types) == 0x54,
               "mac_os_$open_params_t.num_pkt_types");
#endif

/*
 * mac_os_$rcv_pkt_t - the 0x40-byte record a port driver builds for
 * MAC_OS_$DEMUX
 *
 * The driver fills in everything up to +0x30 and MAC_OS_$DEMUX (0x00E0B816)
 * adds the arrival time and the channel it resolved:
 *
 *   0x30  the frame type MAC_OS_$FIND_PACKET_TYPE is asked about
 *         ("move.l (0x30,A4),-(SP)" at 0x00E0B852)
 *   0x2A  the 48-bit arrival time from TIME_$ABS_CLOCK, copied as its
 *         longword and word halves ("move.l (-0x4c,A6),(0x2a,A4)" at
 *         0x00E0B890 and "move.w (-0x48,A6),(0x2e,A4)" at 0x00E0B896)
 *   0x34  the address of the resolved mac_os_$channel_t
 *         ("lea (0x7a0,A2),A0 / move.l A0,(0x34,A4)" at 0x00E0B89C)
 *
 * MAC_OS_$DEMUX reads and writes nothing else in the record; it hands the
 * whole thing to the channel's callback (0x00E0B8A4-0x00E0B8B2).  The named
 * fields below are the ones ring_$receive_packet fills in
 * (0x00E76528-0x00E7656A); the reserved runs are what no code seen so far
 * touches.
 */
typedef struct mac_os_$rcv_pkt_t {
    uint16_t    net_type;       /* 0x00: network type; the ring driver stores 2 */
    uint32_t    src_id;         /* 0x02: source node id (UNALIGNED longword) */
    uint8_t     _r06[0x12];     /* 0x06 */
    boolean     is_local;       /* 0x18: the packet came from this node */
    uint8_t     _r19[0x03];     /* 0x19 */
    int32_t     body_len;       /* 0x1C: bytes of header body after the MAC header */
    uint32_t    body;           /* 0x20: address of that body */
    uint32_t    _r24;           /* 0x24 */
    uint8_t     _r28[0x02];     /* 0x28 */
    uint32_t    time_high;      /* 0x2A: TIME_$ABS_CLOCK high longword (UNALIGNED) */
    uint16_t    time_low;       /* 0x2E: TIME_$ABS_CLOCK low word */
    uint32_t    frame_type;     /* 0x30 */
    uint32_t    channel;        /* 0x34: mac_os_$channel_t * of the receiver */
    uint32_t    data_len;       /* 0x38: payload byte count */
    uint32_t    data_pa;        /* 0x3C: payload DMA address */
} __attribute__((packed)) mac_os_$rcv_pkt_t;

#if defined(ARCH_M68K)
_Static_assert(offsetof(mac_os_$rcv_pkt_t, src_id)     == 0x02, "rcv_pkt.src_id");
_Static_assert(offsetof(mac_os_$rcv_pkt_t, is_local)   == 0x18, "rcv_pkt.is_local");
_Static_assert(offsetof(mac_os_$rcv_pkt_t, body_len)   == 0x1C, "rcv_pkt.body_len");
_Static_assert(offsetof(mac_os_$rcv_pkt_t, body)       == 0x20, "rcv_pkt.body");
_Static_assert(offsetof(mac_os_$rcv_pkt_t, _r24)       == 0x24, "rcv_pkt._r24");
_Static_assert(offsetof(mac_os_$rcv_pkt_t, time_high)  == 0x2A, "rcv_pkt.time_high");
_Static_assert(offsetof(mac_os_$rcv_pkt_t, time_low)   == 0x2E, "rcv_pkt.time_low");
_Static_assert(offsetof(mac_os_$rcv_pkt_t, frame_type) == 0x30, "rcv_pkt.frame_type");
_Static_assert(offsetof(mac_os_$rcv_pkt_t, channel)    == 0x34, "rcv_pkt.channel");
_Static_assert(offsetof(mac_os_$rcv_pkt_t, data_len)   == 0x38, "rcv_pkt.data_len");
_Static_assert(offsetof(mac_os_$rcv_pkt_t, data_pa)    == 0x3C, "rcv_pkt.data_pa");
_Static_assert(sizeof(mac_os_$rcv_pkt_t) == 0x40, "mac_os_$rcv_pkt_t must be 0x40 bytes");
#endif

/*
 * mac_os_$link_addr_t - the link-level address MAC_OS_$ARP resolves
 *
 * MAC_OS_$ARP's third argument (0x00E0C0DE movea.l (0xe,A6),A2) is written as
 * a word count followed by up to three address words: 2 words for Ethernet
 * and network type 3, 3 words for token ring and FDDI.  RING_$SEND_OS refuses
 * anything but a count of 2 ("move.w (A0),D0w / cmpi.w #0x2,D0w / bne" at
 * 0x00E77D7E, status 0x00310012) and then copies two words from +0x02
 * (0x00E77D86-0x00E77D96).
 */
typedef struct mac_os_$link_addr_t {
    uint16_t    n_words;        /* 0x00: 2 or 3 */
    uint16_t    addr[3];        /* 0x02: only n_words entries are meaningful */
} mac_os_$link_addr_t;

/*
 * mac_os_$buf_desc_t - one {length, address, next} buffer descriptor
 *
 * MAC_OS_$SEND walks a chain of these starting at send record +0x1C
 * (0x00E0B62C lea (0x1c,A4),A0; 0x00E0B640 move.l (A1),D0 = length;
 * 0x00E0B64A tst.l (0x4,A1) = address; 0x00E0B660 move.l (0x8,A1) = next).
 * A negative length, or a zero address with a positive length, is rejected
 * with status 0x003A000C.  The addresses are m68k 32-bit addresses, so the
 * fields are uint32_t rather than pointers.
 */
typedef struct mac_os_$buf_desc_t {
    int32_t     length;         /* 0x00 */
    uint32_t    address;        /* 0x04 */
    uint32_t    next;           /* 0x08: 0 = end of chain */
} mac_os_$buf_desc_t;

/*
 * mac_os_$send_pkt_t - the 0x4C-byte descriptor MAC_OS_$SEND hands to the
 * port driver.
 *
 * Size: RING_$SEND_OS copies the whole record with
 * "movea.l D6,A1 / lea (-0x50,A6),A4 / moveq #0x12,D1 / move.l (A1)+,(A4)+ /
 * dbf D1w" at 0x00E77E76-0x00E77E80 - 0x13 longwords = 0x4C bytes.
 *
 * Who writes what:
 *   0x00  MAC_OS_$ARP (ROUTE_$PROCESS 0x00E87690 passes the record itself as
 *         ARP's third argument; MAC_$SEND does the same at 0x00E0BB9C)
 *   0x18  ARP's fourth argument, the broadcast flag ("clr.b (A3)" 0x00E0C112 /
 *         "st (A3)" 0x00E0C12A); RING_$SEND_OS tests it at 0x00E77DB6
 *   0x1C  the caller's header buffer descriptor; MAC_OS_$SEND overwrites it
 *         with its own when hdr_prebuilt is false (0x00E0B772-0x00E0B77C)
 *   0x28  "header already built": MAC_OS_$SEND does "move.b (0x28,A4),D1b /
 *         not.b D1b" at 0x00E0B616 and skips its whole buffer-setup block
 *         when the byte is true
 *   0x30  frame type (0x600 for both ROUTE_$PROCESS 0x00E876BA and
 *         XNS_IDP_$OS_SEND 0x00E183FC); RING_$SEND_OS forwards it at
 *         0x00E77DEA
 *   0x38  payload byte count; written by MAC_OS_$SEND at 0x00E0B786 when it
 *         builds its own buffers, by the caller otherwise
 *   0x3C  payload page addresses.  MAC_OS_$SEND writes only the first
 *         (0x00E0B742 / 0x00E0B76C); ROUTE_$PROCESS (0x00E876CC-0x00E876DA)
 *         and XNS_IDP_$OS_SEND (0x00E1841C-0x00E18428, which copies
 *         data_length and all four pages in one five-longword loop) fill
 *         them all before setting hdr_prebuilt.
 *
 * xns/idp_send.c builds one of these at A6-0x88 (source-tvrs, closed).
 * TODO(source-5lqz): 0x08..0x17 is copied verbatim by MAC_$SEND
 * (0x00E0BBC2 "moveq #0x5" = 6 longwords from the user's record) but no
 * driver seen so far reads it.
 */
typedef struct mac_os_$send_pkt_t {
    mac_os_$link_addr_t link_addr;      /* 0x00: filled in by MAC_OS_$ARP */
    uint8_t     _unknown_08[0x10];      /* 0x08: see the TODO above */
    int8_t      is_broadcast;           /* 0x18: Pascal boolean, 0xFF = true */
    uint8_t     _pad_19[3];             /* 0x19 */
    mac_os_$buf_desc_t hdr_desc;        /* 0x1C: header buffer chain */
    int8_t      hdr_prebuilt;           /* 0x28: Pascal boolean, 0xFF = true */
    uint8_t     _pad_29[7];             /* 0x29 */
    uint32_t    frame_type;             /* 0x30 */
    uint32_t    _pad_34;                /* 0x34 */
    uint32_t    data_length;            /* 0x38 */
    uint32_t    data_pages[4];          /* 0x3C */
} mac_os_$send_pkt_t;

#if defined(ARCH_M68K)
_Static_assert(sizeof(mac_os_$link_addr_t) == 0x08, "mac_os_$link_addr_t must be 8 bytes");
_Static_assert(sizeof(mac_os_$buf_desc_t)  == 0x0C, "mac_os_$buf_desc_t must be 12 bytes");
_Static_assert(offsetof(mac_os_$send_pkt_t, is_broadcast) == 0x18, "send_pkt.is_broadcast");
_Static_assert(offsetof(mac_os_$send_pkt_t, hdr_desc)     == 0x1C, "send_pkt.hdr_desc");
_Static_assert(offsetof(mac_os_$send_pkt_t, hdr_prebuilt) == 0x28, "send_pkt.hdr_prebuilt");
_Static_assert(offsetof(mac_os_$send_pkt_t, frame_type)   == 0x30, "send_pkt.frame_type");
_Static_assert(offsetof(mac_os_$send_pkt_t, data_length)  == 0x38, "send_pkt.data_length");
_Static_assert(offsetof(mac_os_$send_pkt_t, data_pages)   == 0x3C, "send_pkt.data_pages");
_Static_assert(sizeof(mac_os_$send_pkt_t) == 0x4C, "mac_os_$send_pkt_t must be 0x4C bytes");
#endif

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

extern mac_os_$port_pkt_table_t MAC_OS_$PORT_PKT_TABLES[MAC_OS_MAX_PORTS];
extern mac_os_$channel_t MAC_OS_$CHANNEL_TABLE[MAC_OS_MAX_CHANNELS];
extern void *MAC_OS_$EXCLUSION;
extern mac_os_$port_info_t MAC_OS_$PORT_INFO_TABLE[MAC_OS_MAX_PORTS];

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
void MAC_OS_$DEMUX(mac_os_$rcv_pkt_t *pkt_info, int16_t *port_num, void *param3, status_$t *status_ret);

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
