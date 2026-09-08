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
#include "ml/ml.h"
#include "rip/rip.h"   /* rip_$nexthop_t: MAC_OS_$BROADCAST_NEXTHOP */

/*
 * ============================================================================
 * Constants
 * ============================================================================
 */

/* Maximum number of network ports */
#define MAC_OS_MAX_PORTS        8

/* Maximum number of channels */
#define MAC_OS_MAX_CHANNELS     10

/*
 * MAC_OS_$OPEN's free-channel scan tests one slot PAST the ten real ones
 * before it gives up:
 *   0x00E0B2C2  move.w (0x7b2,A0),D0w      test slot n
 *   0x00E0B2C6  btst.l #0x9,D0
 *   0x00E0B2CA  bne  -> 0x00E0B2A8
 *   0x00E0B2A8  cmpi.w #0xa,D2w            only NOW is the index checked
 *   0x00E0B2AC  bcs  -> 0x00E0B2BC         n < 10: step to n + 1 and retest
 * so when slots 0..9 are all in use the loop advances to n = 10, reads
 * A5 + 0x7A0 + 10 * 0x14 = A5 + 0x868 - the first word of MAC_OS_$EXCLUSION -
 * and only rejects the channel if bit 9 happens to be set there.  The C table
 * carries an eleventh slot so that read has real storage; in the image it
 * overlays the lock.
 */
#define MAC_OS_CHANNEL_TABLE_SLOTS  (MAC_OS_MAX_CHANNELS + 1)

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
    uint16_t    channel_index;  /* 0x08: Channel to route packets to */
    uint16_t    reserved;       /* 0x0A: never read or written by this image */
} mac_os_$pkt_type_entry_t;

/*
 * channel_index sits at entry offset 0x08, not 0x0A.  Every user indexes the
 * entry array off the TABLE base (which is 4 bytes ahead of entries[0]), so
 * the displacement in the assembly is 0x0C:
 *   MAC_OS_$OPEN         0x00E0B340  move.l (A1)+,(0x4,A0)   ; range_low
 *                        0x00E0B344  move.l (A1)+,(0x8,A0)   ; range_high
 *                        0x00E0B348  move.w D2w,(0xc,A0)     ; channel_index
 *   MAC_OS_$DEMUX        0x00E0B870  move.w (0xc,A2,D1*0x1),D1w
 *   MAC_OS_$CLOSE        0x00E0B4D0  move.w (0xc,A1),D1w
 *   MAC_OS_$PROC2_CLEANUP 0x00E0C07A cmp.w (0xc,A1),D4w
 * with A0/A1/A2 = &table and D1 = 12 * index.
 */
#if defined(ARCH_M68K)
_Static_assert(offsetof(mac_os_$pkt_type_entry_t, range_low)     == 0x00,
               "mac_os_$pkt_type_entry_t.range_low");
_Static_assert(offsetof(mac_os_$pkt_type_entry_t, range_high)    == 0x04,
               "mac_os_$pkt_type_entry_t.range_high");
_Static_assert(offsetof(mac_os_$pkt_type_entry_t, channel_index) == 0x08,
               "mac_os_$pkt_type_entry_t.channel_index");
_Static_assert(sizeof(mac_os_$pkt_type_entry_t) == 0x0C,
               "mac_os_$pkt_type_entry_t must be 12 bytes");
#endif

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
    /*
     * 0x00 and 0x04 hold target VIRTUAL ADDRESSES, not host pointers, so the
     * entry is 20 bytes on any host (source-ytxx).  Reach them with
     * ARCH_VA_TO_PTR / ARCH_PTR_TO_VA, exactly as route_$driver_info_t is
     * handled.  The image loads and calls them as longwords:
     *   0x00E0B882  tst.l (0x7a0,A2)          the callback, tested for NIL
     *   0x00E0B8AE  movea.l (0x7a0,A2),A0     ... then jsr (A0)
     *   0x00E0B5E4  movea.l (0x7a4,A3),A0     the driver record
     */
    uint32_t    callback;       /* 0x00: VA of the receive callback */
    uint32_t    driver_info;    /* 0x04: VA of the driver info record */
    uint16_t    socket;         /* 0x08: Socket number (0xE1 = no socket) */
    uint16_t    port_index;     /* 0x0A: Port number (0-7) */
    uint16_t    callback_data;  /* 0x0C: Saved callback data */
    uint16_t    line_number;    /* 0x0E: Line number */
    uint16_t    header_size;    /* 0x10: Header size for this network type */
    uint16_t    flags;          /* 0x12: Channel flags.  Every site reaches
                                 * this word through the BYTE at entry offset
                                 * 0x12, which big-endian m68k makes its HIGH
                                 * half, so a "bclr.b #n" there is word bit
                                 * n + 8:
                                 *   Bit  8 (0x0100): promiscuous
                                 *   Bit  9 (0x0200): channel in use
                                 *   Bits 10-15:      owner AS_ID
                                 */
} mac_os_$channel_t;

/* No host pointers in the record, so these hold on every target. */
_Static_assert(__builtin_offsetof(mac_os_$channel_t, callback) == 0x00,
               "mac_os_$channel_t.callback");
_Static_assert(__builtin_offsetof(mac_os_$channel_t, driver_info) == 0x04,
               "mac_os_$channel_t.driver_info");
_Static_assert(__builtin_offsetof(mac_os_$channel_t, socket) == 0x08,
               "mac_os_$channel_t.socket");
_Static_assert(__builtin_offsetof(mac_os_$channel_t, port_index) == 0x0A,
               "mac_os_$channel_t.port_index");
_Static_assert(__builtin_offsetof(mac_os_$channel_t, callback_data) == 0x0C,
               "mac_os_$channel_t.callback_data");
_Static_assert(__builtin_offsetof(mac_os_$channel_t, line_number) == 0x0E,
               "mac_os_$channel_t.line_number");
_Static_assert(__builtin_offsetof(mac_os_$channel_t, header_size) == 0x10,
               "mac_os_$channel_t.header_size");
_Static_assert(__builtin_offsetof(mac_os_$channel_t, flags) == 0x12,
               "mac_os_$channel_t.flags");
_Static_assert(sizeof(mac_os_$channel_t) == 20, "mac_os_$channel_t must be 20 bytes");

/*
 * mac_os_$channel_t.flags bits, as the word at entry offset 0x12.
 *
 * The byte at A5 + 0x7B2 + 0x14*channel is that word's high half, so every
 * byte-sized operation on it names a bit eight higher in the word:
 *   MAC_OS_$INIT          0x00E2F5FE  bclr.b #0x0,(0x7b2,A0)  -> 0x0100
 *                         0x00E2F604  bclr.b #0x1,(0x7b2,A0)  -> 0x0200
 *   MAC_OS_$OPEN          0x00E0B36C  bset.b #0x1,(0x7b2,A0)  -> 0x0200
 *                         0x00E0B372  andi.b #0x3,(0x7b2,A0)  -> keep 0x0300
 *                         0x00E0B380  or.b   D1b,(0x7b2,A0)   -> asid << 10
 *   MAC_$OPEN             0x00E0BA30  andi.b #-0x2,(0x7b2,A0) -> clear 0x0100
 *                         0x00E0BA36  or.b   D1b,(0x7b2,A0)   -> set 0x0100
 *   MAC_OS_$CLOSE         0x00E0B500  bclr.b #0x1,(0x7b2,A2)  -> 0x0200
 *   MAC_OS_$PROC2_CLEANUP 0x00E0C020  bclr.b #0x1,(0x7b2,A2)  -> 0x0200
 * while the two readers use word operations and say bit 9 outright
 * ("move.w (0x7b2,A0),D0w / btst.l #0x9,D0" at 0x00E0B2C2 and 0x00E0C004).
 */
#define MAC_OS_CHANNEL_PROMISCUOUS  0x0100  /* set from mac_$open_params_t.flags bit 7 */
#define MAC_OS_CHANNEL_IN_USE       0x0200  /* btst #9 in MAC_$CLOSE (0x00E0BAA8) */
#define MAC_OS_CHANNEL_OWNER_MASK   0xFC00  /* owner AS id, shifted left by 10 */
#define MAC_OS_CHANNEL_OWNER_SHIFT  10

/* Value MAC_$CLOSE writes into .socket to mark the channel free */
#define MAC_OS_CHANNEL_NO_SOCKET    0xE1

/*
 * Port info entry (8 bytes)
 *
 * MAC_OS_$INIT writes all three fields per port (0x00E2F55E-0x00E2F582,
 * base A5 + 0x89C, stride 8):
 *   move.l #0x1,(0x89c,A1)          version  = 1
 *   clr.w  (0x8a0,A1)               config   = 0
 *   move.w (0x4,A0),(0x8a2,A1)      mtu      = driver_info->mtu
 * and MAC_OS_$PUT_INFO replaces the whole 8 bytes with OS_$DATA_COPY.
 */
typedef struct mac_os_$port_info_t {
    uint32_t    version;        /* 0x00: Port version (must be 1) */
    uint16_t    config;         /* 0x04: Port configuration */
    uint16_t    mtu;            /* 0x06: MTU copied from the driver info */
} mac_os_$port_info_t;

#if defined(ARCH_M68K)
_Static_assert(__builtin_offsetof(mac_os_$port_info_t, config) == 0x04,
               "mac_os_$port_info_t.config");
_Static_assert(__builtin_offsetof(mac_os_$port_info_t, mtu) == 0x06,
               "mac_os_$port_info_t.mtu");
_Static_assert(sizeof(mac_os_$port_info_t) == 8, "mac_os_$port_info_t must be 8 bytes");
#endif

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
 * mac_os_$link_addr_t - the variable-length link-level address that heads
 * every MAC packet descriptor.  24 bytes: a word count followed by up to 11
 * address words (source-txfx).
 *
 * SHAPE.  Two count-driven copy loops give the {count, words} shape directly:
 *   MAC_$DEMUX   0x00E0BC82  move.w (A2),(-0x2e,A6)        ; copy the count
 *                0x00E0BC86  move.w (A2),D0w / subq.w #0x1,D0w / bmi
 *                0x00E0BC90  move.w (0x2,A1),(-0x2c,A0)    ; count words
 *                0x00E0BC96  addq.l #0x2,A0 / addq.l #0x2,A1 / dbf
 *   MAC_$RECEIVE 0x00E0BE52  the same loop the other way round
 *                0x00E0BE60  move.w (-0x2c,A0),(0x2,A1)
 * The word at +0x00 is the number of address words; the words follow at
 * +0x02.  Neither loop bounds the count, so a count above 11 runs off the
 * end of the record - that is the original behaviour, not a transcription
 * error.
 *
 * EXTENT = 24 bytes, hence 11 words:
 *   - in the driver record the next field is the boolean at +0x18
 *     (0x00E0BC68 tst.b (0x18,A2) in MAC_$DEMUX, 0x00E0BE4E
 *     move.b D1b,(0x18,A3) in MAC_$RECEIVE), and MAC_$SEND copies exactly
 *     0x00..0x17 as one record assignment (0x00E0BBC2 moveq #0x5 +
 *     move.l (A0)+,(A1)+ / dbf = 6 longwords) before filling +0x18, +0x1C,
 *     +0x28, +0x30, +0x38 and +0x3C field by field (0x00E0BBCE-0x00E0BBF0);
 *   - in MAC_$DEMUX's staging record the count is at A6-0x2E, the words
 *     start at A6-0x2C, and the next field written is the word at A6-0x16
 *     (0x00E0BCBC move.w (0x3a,A2),(-0x16,A6)), leaving 0x16 bytes = 11
 *     words for the address.
 *
 * COUNTS this image actually stores:
 *   2  MAC_OS_$ARP for route_port_t.net_type 0 and 3 - the 20-bit Apollo
 *      ring node id taken out of the IP address as (ip >> 16) & 0x000F and
 *      ip & 0xFFFF (0x00E0C1A4 move.w #0x2,(A2) .. 0x00E0C1B2); the
 *      broadcast arm writes the count alone (0x00E0C14E move.w #0x2,(A2)).
 *   3  MAC_OS_$ARP for net_type 4 and 5 - a 6-byte IEEE 802 address, copied
 *      word by word from the caller's address record (0x00E0C1FC
 *      move.w #0x3,(A2) .. 0x00E0C210 dbf) or set to FF-FF-FF-FF-FF-FF for
 *      broadcast (0x00E0C156 move.w #0x3,(A2) .. 0x00E0C164 dbf).
 *   2  ring_$receive_packet, followed by the two words of the ring source
 *      node id (0x00E76528 move.w #0x2,(-0x50,A6); 0x00E7652E-0x00E7653E).
 *
 * The only length check in the image is RING_$SEND_OS's, which refuses
 * anything but 2 (0x00E77D7E move.w (A0),D0w / cmpi.w #0x2,D0w / bne ->
 * status 0x00310012) and then copies two words from +0x02 into the ring
 * header (0x00E77D86-0x00E77D96).
 *
 * What the remaining eight words are for is UNATTESTED in this image: no
 * count above 3 is ever stored, the only other MAC port driver present is
 * ETHERNET_$INIT (0x00E78004), a stub that returns
 * status_$io_controller_not_in_system, and the SR10.4 user-space /sys/ins
 * tree ships no mac.ins.pas to name the record.
 */
#define MAC_OS_MAX_ADDR_WORDS   11      /* 0x18 bytes - 1 count word */

typedef struct mac_os_$link_addr_t {
    uint16_t    n_words;                        /* 0x00: 2 or 3 in this image */
    uint16_t    addr[MAC_OS_MAX_ADDR_WORDS];    /* 0x02: n_words are meaningful */
} mac_os_$link_addr_t;

/*
 * mac_os_$rcv_pkt_t - the 0x4C-byte record a port driver builds for
 * MAC_OS_$DEMUX.  It is the same Pascal record as mac_os_$send_pkt_t: the
 * link address at 0x00..0x17, a boolean at 0x18, a buffer chain at 0x1C,
 * the frame type at 0x30, the payload length at 0x38 and the payload pages
 * at 0x3C all sit at the same offsets in both.
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
    /*
     * 0x00..0x17 is the same mac_os_$link_addr_t that heads the send record:
     * MAC_$DEMUX copies it out of here with the count-driven word loop at
     * 0x00E0BC82-0x00E0BC9A (source-txfx).  The second arm names the only
     * case this image builds - ring_$receive_packet's n_words == 2 followed
     * by the two words of the source node id (0x00E76528, 0x00E7652E).
     */
    union {
        mac_os_$link_addr_t link_addr;  /* 0x00 */
        struct {
            uint16_t    net_type;       /* 0x00: == link_addr.n_words */
            uint32_t    src_id;         /* 0x02: UNALIGNED longword */
            uint8_t     _r06[0x12];     /* 0x06: == link_addr.addr[2..10] */
        } __attribute__((packed));
    };
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
    uint32_t    data_pa[4];     /* 0x3C: payload DMA addresses.  MAC_$DEMUX
                                 * copies all four with "lea (0x3c,A2),A0" and
                                 * four "move.l (A0)+,(A1)+" (0x00E0BCC2), the
                                 * same four slots mac_os_$send_pkt_t.data_pages
                                 * holds, so the record runs to 0x4C.
                                 * ring_$receive_packet writes only the first
                                 * (0x00E7654A) and builds the record in a
                                 * 0x50-byte frame slot at A6-0x50. */
} __attribute__((packed)) mac_os_$rcv_pkt_t;

#if defined(ARCH_M68K)
_Static_assert(offsetof(mac_os_$rcv_pkt_t, link_addr)  == 0x00, "rcv_pkt.link_addr");
_Static_assert(offsetof(mac_os_$rcv_pkt_t, net_type)   == 0x00, "rcv_pkt.net_type");
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
_Static_assert(sizeof(mac_os_$rcv_pkt_t) == 0x4C, "mac_os_$rcv_pkt_t must be 0x4C bytes");
#endif


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
 *
 * Bytes 0x00..0x17 are ONE mac_os_$link_addr_t, not an 8-byte address plus
 * 16 spare bytes: MAC_$SEND's 0x00E0BBC2 block copy moves them as a single
 * record assignment, and MAC_$DEMUX / MAC_$RECEIVE walk them with a
 * count-driven word loop.  See mac_os_$link_addr_t above (source-txfx).
 */
typedef struct mac_os_$send_pkt_t {
    mac_os_$link_addr_t link_addr;      /* 0x00..0x17: filled in by MAC_OS_$ARP */
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
_Static_assert(offsetof(mac_os_$link_addr_t, addr) == 0x02, "link_addr.addr");
_Static_assert(sizeof(mac_os_$link_addr_t) == 0x18, "mac_os_$link_addr_t must be 0x18 bytes");
_Static_assert(sizeof(mac_os_$buf_desc_t)  == 0x0C, "mac_os_$buf_desc_t must be 12 bytes");
_Static_assert(offsetof(mac_os_$send_pkt_t, link_addr)    == 0x00, "send_pkt.link_addr");
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

/*
 * MAC_OS_$PORT_PKT_TABLES - per-port packet-type tables, MAC_OS_$DATA + 0
 * (0x00E22990).  MAC_OS_$INIT clears each table's entry_count through a
 * pointer it advances by 0xF4 per port (0x00E2F536 / 0x00E2F558 /
 * 0x00E2F5E6), and 8 * 0xF4 = 0x7A0, exactly the displacement of
 * MAC_OS_$CHANNEL_TABLE.  The map gives the segment base no interior symbol,
 * so this stays a tree name.
 */
/*
 * MAC_OS_$BROADCAST_NEXTHOP - the constant next-hop record at
 * MAC_OS_$DATA + 0x8E0 (0x00E23270), the last object in the module block
 * (`D E22990 MAC_OS size = 8EC`).
 *
 * MAC_$SEND hands it to MAC_OS_$ARP as the address to resolve
 * (0x00E0BBA6 `pea (0x8e0,A5)`), and the image bytes are
 *
 *   00e23270  00 00 00 00  ff ff  ff ff ff ff  00 00
 *
 * i.e. network 0 and an all-ones host address, which is exactly what
 * MAC_OS_$ARP's broadcast test looks for (0x00E0C18E `cmpi.w #0x800,(0x4,A1)`
 * fails, then the all-ones comparison succeeds and it answers with the
 * broadcast link address and sets the caller's flag byte).  The map gives it
 * no symbol, so the name is a tree name.
 */
extern const rip_$nexthop_t MAC_OS_$BROADCAST_NEXTHOP;

extern mac_os_$port_pkt_table_t MAC_OS_$PORT_PKT_TABLES[MAC_OS_MAX_PORTS];
/*
 * MAC_OS_$CHANNEL_TABLE - per-channel receive state, 10 entries of 20 bytes
 * Address: 0x00E23130 (MAC_OS_$DATA + 0x7A0); see mac_os/mac_os_data.c for
 * how the base is pinned.
 */
extern mac_os_$channel_t MAC_OS_$CHANNEL_TABLE[MAC_OS_CHANNEL_TABLE_SLOTS];
/*
 * MAC_OS_$EXCLUSION - lock guarding MAC_OS_$CHANNEL_TABLE
 * Address: 0x00E231F8 (MAC_OS_$DATA + 0x868)
 */
extern ml_$exclusion_t MAC_OS_$EXCLUSION;
/*
 * MAC_OS_$PORTP_TABLE - one pointer per port to that port's
 * MAC_OS_$PORT_TABLE entry.  MAC_OS_$INIT fills it with
 * `lea (0x89c,A0),A3 / move.l A3,(0x87c,A4)` (0x00E2F54C), A4 advancing by 4
 * per port, so it is 8 pointers at MAC_OS_$DATA + 0x87C - immediately after
 * MAC_OS_$EXCLUSION (0x868 + 0x14) and immediately before MAC_OS_$PORT_TABLE.
 * Named by the SAU2 map.
 *
 * Address: 0x00E2320C
 */
extern mac_os_$port_info_t *MAC_OS_$PORTP_TABLE[MAC_OS_MAX_PORTS];

/*
 * MAC_OS_$PORT_TABLE - per-port version/config/mtu records, MAC_OS_$DATA +
 * 0x89C, stride 8 (see mac_os_$port_info_t above).  Named by the SAU2 map;
 * the tree previously called it MAC_OS_$PORT_INFO_TABLE.
 *
 * Address: 0x00E2322C
 */
extern mac_os_$port_info_t MAC_OS_$PORT_TABLE[MAC_OS_MAX_PORTS];

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
