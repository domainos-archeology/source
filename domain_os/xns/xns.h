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
#include "mac_os/mac_os.h"   /* mac_os_$buf_desc_t */
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
} __attribute__((packed)) xns_$net_addr_t;

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
} __attribute__((packed)) xns_$idp_header_t;

/*
 * The 24 bytes at header +0x06..+0x1D are exactly the destination address
 * followed by the source address.  XNS_IDP_$OS_SEND fills them either with
 * one 24-byte `move.b (A3)+,(A4)+' / `dbf' loop out of its request record
 * (0x00E1833C) or with two three-longword copies out of the channel's
 * dest_network / src_network (0x00E18318-0x00E18332); RIP_$SEND_TO_PORT
 * does the same pair of copies at 0x00E8712E / 0x00E8713C.
 */
/*
 * Both structures are `packed' so that the host build lays them out the way
 * m68k does.  m68k aligns 32-bit scalars on two-byte boundaries, so the
 * unpacked declaration happened to be right for the target but put
 * dest_network at +0x08 on a host with four-byte alignment.  The remaining
 * offsets are asserted next to ROUTE_$PROCESS's accessors further down.
 */
_Static_assert(sizeof(xns_$net_addr_t) == 12, "xns_$net_addr_t must be 12 bytes");
_Static_assert(sizeof(xns_$idp_header_t) == XNS_IDP_HEADER_SIZE,
               "xns_$idp_header_t must be 30 bytes");
_Static_assert(offsetof(xns_$idp_header_t, src_host) == 0x16, "idp_header src_host at +0x16");

/*
 * XNS IDP per-port state (0x0C = 12 bytes per port)
 *
 * The port array lives at state +0x40 and is indexed by the ROUTE port
 * number.  Recovered from xns_$add_port (0x00E17BF8) and
 * xns_$delete_port (0x00E17CB2), which compute
 * `lea (0,A5,port*0xC),A3' and then use (0x40,A3) / (0x48,A3) / (0x4A,A3),
 * and from xns_$is_broadcast_addr (0x00E17E88), which walks eight entries
 * reading (0x44,A2) with `lea (0xC,A2),A2'.
 */
typedef struct xns_$port_state_t {
  uint32_t mac_handle;      /* 0x00 (state +0x40): MAC-layer handle */
  uint32_t net_addr_ptr;    /* 0x04 (state +0x44): -> this port's network number */
  uint16_t mac_socket;      /* 0x08 (state +0x48): MAC socket (0xFFFF = closed) */
  int16_t  refcount;        /* 0x0A (state +0x4A): channels using this port */
} xns_$port_state_t;

/*
 * XNS IDP Channel State (0x48 = 72 bytes per channel)
 *
 * Each channel maintains state for an IDP endpoint.  The channel array
 * begins at state +0xA0 and the stride is 0x48; see XNS_IDP_$OS_DEMUX
 * (0x00E185BA..0x00E185EE): `movea.l A5,A1' ... `lea (0x48,A1),A1' with the
 * socket read as (0xD8,A0) and the demux vector as (0xA0,A2).  0xA0 +
 * 16 * 0x48 == 0x520, which is exactly where the exclusion lock starts,
 * confirming both the base and the element count.
 *
 * Field offsets below are given as `channel-relative (state-relative for
 * channel 0)'.
 */
typedef struct xns_$channel_t {
  code_ptr_t demux;         /* 0x00 (+0xA0): demux callback (NULL = unused) */

  /* Connected destination address (for connected mode) */
  uint32_t dest_network;    /* 0x04 (+0xA4): destination network */
  uint8_t  dest_host[6];    /* 0x08 (+0xA8): destination host */
  uint16_t dest_socket;     /* 0x0E (+0xAE): destination socket */

  /* Bound local address (for XNS_OPEN_FLAG_BIND_LOCAL) */
  uint32_t src_network;     /* 0x10 (+0xB0): source network */
  uint8_t  src_host[6];     /* 0x14 (+0xB4): source host */
  uint16_t src_port;        /* 0x1A (+0xBA): source socket */

  uint8_t  mac_info[0x18];  /* 0x1C (+0xBC): MAC info for ARP etc. */

  int16_t  connected_port;  /* 0x34 (+0xD4): port index (-1 = any) */
  uint16_t user_socket;     /* 0x36 (+0xD6): user socket (XNS_NO_SOCKET = none) */
  int16_t  xns_socket;      /* 0x38 (+0xD8): XNS socket number */
  uint16_t flags;           /* 0x3A (+0xDA): flags and owning AS_ID */
  uint8_t  port_active[8];  /* 0x3C (+0xDC): per-port active flags */
  int16_t  state;           /* 0x44 (+0xE4): channel state (bit 15 = active) */
  uint16_t _pad_46;         /* 0x46: padding to the 0x48 stride */
} xns_$channel_t;

/*
 * PACKED: mac_src_hi sits at +0x26 and mac_src_lo at +0x2A, both two bytes off
 * a longword boundary.  m68k aligns 32-bit fields to two bytes, so the record
 * needs no padding there on the target; a host that aligns uint32_t to four
 * would insert some, which is why the record is packed.  Packing changes no
 * m68k layout.
 *
 * Packet descriptor handed to a channel's demux callback (0x48 bytes).
 *
 * XNS_IDP_$OS_DEMUX builds this record in its own frame at A6-0x88 and
 * passes `pea (-0x88,A6)' both to XNS_ERROR_$SEND (which reads (0x18,A1)
 * and (0x1C,A1) at 0x00E17A74/0x00E17A7E) and to the channel's demux
 * vector (0x00E18650).  The offsets below are therefore record-relative,
 * NOT A6 displacements.
 *
 * This record has the same shape as the MAC receive descriptor that
 * XNS_IDP_$OS_DEMUX is handed, shifted down by four bytes; see
 * xns_$mac_rcv_t.
 */
typedef struct xns_$pkt_desc_t {
  uint8_t  _unknown_00[0x18];       /* 0x00: not written by XNS_IDP_$OS_DEMUX */
  uint32_t data_len;                /* 0x18: IDP data length (mac +0x1C) */
  uint32_t header;                  /* 0x1C: IDP header (mac +0x20).  A target
                                     *       VA, not a C pointer: a real
                                     *       pointer is eight bytes on a
                                     *       64-bit host and would push every
                                     *       later field out of place.  Use
                                     *       ARCH_VA_TO_PTR / ARCH_PTR_TO_VA.
                                     *       With its two neighbours it also
                                     *       forms the mac_os_$buf_desc_t at
                                     *       +0x18 that XNS_ERROR_$SEND takes
                                     *       the address of (0x00E17880
                                     *       "lea (0x18,A0),A0"). */
  uint32_t iov;                     /* 0x20: buffer chain (mac +0x24) */
  boolean  from_net;                /* 0x24: `st' - packet came off the net */
  uint8_t  _unknown_25;             /* 0x25 */
  uint32_t mac_src_hi;              /* 0x26: MAC source, high 4 bytes (mac +0x2A) */
  uint16_t mac_src_lo;              /* 0x2A: MAC source, low 2 bytes (mac +0x2E) */
  uint16_t pkt_len;                 /* 0x2C: NOT written by XNS_IDP_$OS_DEMUX */
  uint16_t _unknown_2e;             /* 0x2E */
  uint32_t channel;                 /* 0x30: receiving channel, again a target
                                     *       VA (0x00E1861A stores A4, the
                                     *       channel's own address) */
  /*
   * 0x34..0x47 arrives as one 20-byte block (XNS_IDP_$OS_DEMUX copies five
   * longwords from mac +0x38 with `moveq #0x4,D2` + `dbf` at 0x00E1852C).
   * Three readers in the image slice it three different ways, so it is
   * modelled as a union of the slicings rather than one of them:
   *
   *   - xns_$setup_error_header (0x00E17960) reads +0x34 as a LONGWORD byte
   *     count (0x00E179BC `cmp.l (0x34,A0),D5`) and +0x38 as a LONGWORD
   *     network-buffer handle (0x00E179D2 `move.l (0x38,A0),-(SP)` into
   *     NETBUF_$GETVA).
   *   - XNS_IDP_$OS_DEMUX's forwarding arm reads only the WORD at +0x36
   *     (0x00E186D2 `move.w (0x3a,A0),(-0x16,A6)`, mac +0x3A = this record's
   *     +0x36) into xns_$sock_pkt_t.port_info, and copies the 16 bytes from
   *     +0x38 (0x00E186D8 `lea (0x3c,A0),A1` + four `move.l`) into that
   *     record's mac_info.
   *
   * On a big-endian target netbuf_len is (_unknown_34 << 16) | port_info and
   * netbuf_handle is the first four bytes of mac_info; on a little-endian
   * host the views of course disagree numerically, which is why nothing may
   * convert between them by hand.
   */
  union {
    struct {
      uint32_t netbuf_len;          /* 0x34: bytes reachable through the
                                     *       current network-buffer page */
      uint32_t netbuf_handle;       /* 0x38: NETBUF_$GETVA / NETBUF_$RTNVA */
      uint8_t  _netbuf_rest[0x0C];  /* 0x3C */
    };
    struct {
      uint16_t _unknown_34;         /* 0x34: from mac +0x38 */
      uint16_t port_info;           /* 0x36: from mac +0x3A */
      uint8_t  mac_info[0x10];      /* 0x38: from mac +0x3C (16 bytes) */
    };
  };
} __attribute__((packed)) xns_$pkt_desc_t;   /* see the note below */

/*
 * MAC-layer receive descriptor passed to XNS_IDP_$OS_DEMUX.
 *
 * Every field XNS_IDP_$OS_DEMUX reads out of its first argument sits
 * exactly four bytes above the corresponding field of the record it
 * builds for the channel callback (pkt +0x1C -> rec +0x18, pkt +0x20 ->
 * rec +0x1C, pkt +0x2A -> rec +0x26, pkt +0x38 -> rec +0x34, ...), so the
 * descriptor is modelled as a four-byte prefix followed by the same
 * layout.
 */
typedef struct xns_$mac_rcv_t {
  uint32_t _unknown_00;             /* 0x00: not touched by XNS_IDP_$OS_DEMUX */
  xns_$pkt_desc_t d;                /* 0x04 */
} __attribute__((packed)) xns_$mac_rcv_t;

/*
 * Packet record queued on a socket by SOCK_$PUT (0x40 bytes).
 *
 * Built at A6-0x40 by both XNS_IDP_$OS_DEMUX (forwarding path,
 * 0x00E186AE..0x00E186F8) and XNS_IDP_$DEMUX (0x00E18BA2..0x00E18C34).
 * SOCK_$PUT takes the address of this record as its `pkt_ptr' argument,
 * whose first longword is the packet pointer.
 */
typedef struct xns_$sock_pkt_t {
  uint32_t header;                  /* 0x00: the IDP packet, a target VA for
                                     *       the same reason as
                                     *       xns_$pkt_desc_t.header */
  uint32_t mac_src_hi;              /* 0x04: MAC source, high 4 bytes */
  uint16_t mac_src_lo;              /* 0x08: MAC source, low 2 bytes */
  uint16_t _unknown_0a;             /* 0x0A: never written */
  uint32_t data_len;                /* 0x0C: packet length */
  uint16_t flags;                   /* 0x10: XNS_SOCK_PKT_F_* */
  uint16_t reserved_12;             /* 0x12: `clr.w' at 0x00E186E8/0x00E18C0E */
  uint8_t  _unknown_14[0x16];       /* 0x14..0x29: never written */
  uint16_t port_info;               /* 0x2A */
  uint16_t header_len;              /* 0x2C */
  uint16_t _unknown_2e;             /* 0x2E: never written */
  uint8_t  mac_info[0x10];          /* 0x30..0x3F: 16 bytes (4 * move.l) */
} xns_$sock_pkt_t;

/* Flags word at xns_$sock_pkt_t +0x10 */
#define XNS_SOCK_PKT_F_BROADCAST 0x0001 /* bset.b #0,(-0x2f,A6): dest host is broadcast */
#define XNS_SOCK_PKT_F_IDP       0x0002 /* move.w #2: always set by the IDP demux */
#define XNS_SOCK_PKT_F_MAC_BCAST 0x0004 /* bset.b #2: XNS_IDP_$OS_DEMUX arg 3 was true */

/*
 * Channel flags - the 16-bit word xns_$channel_t.flags (channel +0x3A,
 * state +0xDA).
 *
 * XNS_IDP_$OS_OPEN builds it from the open-option byte at opt +0x03:
 * "andi.b #0x7,(0xda,A0) / move.b (0x3,A1),D1b / lsl.b #0x3,D1b /
 * or.b D1b,(0xda,A0)" at 0x00E1817A-0x00E18186.  Because those are BYTE
 * operations on the HIGH byte of the word, open-option bit n lands in word
 * bit n+11.  The AS_ID then goes into bits 5..10 with a WORD operation
 * ("andi.w #-0x7e1 / lsl.w #0x5 / or.w" at 0x00E18192-0x00E1819A), which is
 * why the two ranges do not collide.
 *
 * XNS_IDP_$OS_SEND reads them back with byte btst: "btst.b #0x5,(0xda,A2)"
 * (0x00E182A4) is word bit 13 and "btst.b #0x3,(0xda,A2)" (0x00E182AC) is
 * word bit 11.
 */
#define XNS_CHAN_FLAG_BUILD_HEADER 0x0800 /* Bit 11 (opt bit 0): OS_SEND builds the IDP header */
#define XNS_CHAN_FLAG_BIND_LOCAL 0x1000 /* Bit 12 (opt XNS_OPEN_FLAG_BIND_LOCAL) */
#define XNS_CHAN_FLAG_CONNECT 0x2000    /* Bit 13 (opt XNS_OPEN_FLAG_CONNECT) */
#define XNS_CHAN_FLAG_NO_ALLOC 0x4000   /* Bit 14 (opt XNS_OPEN_FLAG_NO_ALLOC) */
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
  uint32_t packets_sent;     /* 0x000: Total packets sent */
  uint32_t packets_received; /* 0x004: Total packets received (A5+0x4) */
  uint32_t packets_dropped;  /* 0x008: Total packets dropped/errored (A5+0x8) */

  uint8_t _unknown_0c[0x14]; /* 0x00C-0x01F */

  /*
   * Registered local host addresses, six bytes each, `registered_count'
   * being the dbf count (so entries 0..registered_count are valid).
   * xns_$is_broadcast_addr walks these with `lea (A5),A1' /
   * `addq.l #6,A1' reading (0x20,A1) (0x22,A1) (0x24,A1).
   */
  uint8_t addrs[XNS_MAX_ADDRS][6]; /* 0x020-0x037 */

  uint8_t _unknown_38[8];    /* 0x038-0x03F */

  xns_$port_state_t ports[XNS_MAX_PORTS];    /* 0x040-0x09F (8 * 0x0C) */
  xns_$channel_t channels[XNS_MAX_CHANNELS]; /* 0x0A0-0x51F (16 * 0x48) */

  /* Synchronization */
  ml_$exclusion_t lock; /* 0x520: Exclusion lock (ml_$exclusion_t is 0x12 bytes,
                         *        so it occupies 0x520-0x531) */
  uint8_t _pad532[2];   /* 0x532 */

  /* Global counters */
  uint16_t open_channels;   /* 0x534: Number of open channels */
  uint16_t next_socket;     /* 0x536: Next dynamic socket number */
  int16_t registered_count; /* 0x538: dbf count of addrs[] entries */
} xns_$idp_state_t;

/*
 * Layout assertions.  Every offset below was read directly out of the
 * disassembly; see the comments on the individual structures.
 *
 * The two packet records hold no C pointers (bead source-ronb), so their
 * layout is the same on the host and these checks run unguarded.
 */
_Static_assert(sizeof(xns_$pkt_desc_t) == 0x48, "xns_$pkt_desc_t is 0x48 bytes");
_Static_assert(offsetof(xns_$pkt_desc_t, data_len) == 0x18, "pkt_desc data_len at +0x18");
_Static_assert(offsetof(xns_$pkt_desc_t, header) == 0x1C, "pkt_desc header at +0x1C");
_Static_assert(offsetof(xns_$pkt_desc_t, iov) == 0x20, "pkt_desc iov at +0x20");
_Static_assert(offsetof(xns_$pkt_desc_t, from_net) == 0x24, "pkt_desc from_net at +0x24");
_Static_assert(offsetof(xns_$pkt_desc_t, mac_src_hi) == 0x26, "pkt_desc mac_src_hi at +0x26");
_Static_assert(offsetof(xns_$pkt_desc_t, mac_src_lo) == 0x2A, "pkt_desc mac_src_lo at +0x2A");
_Static_assert(offsetof(xns_$pkt_desc_t, pkt_len) == 0x2C, "pkt_desc pkt_len at +0x2C");
_Static_assert(offsetof(xns_$pkt_desc_t, channel) == 0x30, "pkt_desc channel at +0x30");
_Static_assert(offsetof(xns_$pkt_desc_t, netbuf_len) == 0x34, "pkt_desc netbuf_len at +0x34");
_Static_assert(offsetof(xns_$pkt_desc_t, port_info) == 0x36, "pkt_desc port_info at +0x36");
_Static_assert(offsetof(xns_$pkt_desc_t, netbuf_handle) == 0x38, "pkt_desc netbuf_handle at +0x38");
_Static_assert(offsetof(xns_$pkt_desc_t, mac_info) == 0x38, "pkt_desc mac_info at +0x38");

_Static_assert(offsetof(xns_$mac_rcv_t, d) == 0x04, "mac_rcv payload at +0x04");
_Static_assert(offsetof(xns_$mac_rcv_t, d.header) == 0x20, "mac_rcv header at +0x20");
_Static_assert(offsetof(xns_$mac_rcv_t, d.mac_src_hi) == 0x2A, "mac_rcv mac_src_hi at +0x2A");
_Static_assert(offsetof(xns_$mac_rcv_t, d.netbuf_len) == 0x38, "mac_rcv netbuf_len at +0x38");
_Static_assert(offsetof(xns_$mac_rcv_t, d.mac_info) == 0x3C, "mac_rcv mac_info at +0x3C");

_Static_assert(sizeof(xns_$sock_pkt_t) == 0x40, "xns_$sock_pkt_t is 0x40 bytes");
_Static_assert(offsetof(xns_$sock_pkt_t, header) == 0x00, "sock_pkt header at +0x00");
_Static_assert(offsetof(xns_$sock_pkt_t, mac_src_hi) == 0x04, "sock_pkt mac_src_hi at +0x04");
_Static_assert(offsetof(xns_$sock_pkt_t, mac_src_lo) == 0x08, "sock_pkt mac_src_lo at +0x08");
_Static_assert(offsetof(xns_$sock_pkt_t, data_len) == 0x0C, "sock_pkt data_len at +0x0C");
_Static_assert(offsetof(xns_$sock_pkt_t, flags) == 0x10, "sock_pkt flags at +0x10");
_Static_assert(offsetof(xns_$sock_pkt_t, reserved_12) == 0x12, "sock_pkt reserved_12 at +0x12");
_Static_assert(offsetof(xns_$sock_pkt_t, port_info) == 0x2A, "sock_pkt port_info at +0x2A");
_Static_assert(offsetof(xns_$sock_pkt_t, header_len) == 0x2C, "sock_pkt header_len at +0x2C");
_Static_assert(offsetof(xns_$sock_pkt_t, mac_info) == 0x30, "sock_pkt mac_info at +0x30");

/*
 * The rest still contain native pointers or an embedded ml_$exclusion_t, so
 * they only lay out to the binary on the 32-bit target.
 */
#if defined(ARCH_M68K)
_Static_assert(sizeof(xns_$port_state_t) == 0x0C, "xns_$port_state_t is 0x0C bytes");
_Static_assert(sizeof(xns_$channel_t) == 0x48, "xns_$channel_t is 0x48 bytes");
_Static_assert(offsetof(xns_$channel_t, demux) == 0x00, "channel demux at +0x00");
_Static_assert(offsetof(xns_$channel_t, connected_port) == 0x34, "channel connected_port at +0x34");
_Static_assert(offsetof(xns_$channel_t, user_socket) == 0x36, "channel user_socket at +0x36");
_Static_assert(offsetof(xns_$channel_t, xns_socket) == 0x38, "channel xns_socket at +0x38");
_Static_assert(offsetof(xns_$channel_t, flags) == 0x3A, "channel flags at +0x3A");
_Static_assert(offsetof(xns_$channel_t, port_active) == 0x3C, "channel port_active at +0x3C");
_Static_assert(offsetof(xns_$channel_t, state) == 0x44, "channel state at +0x44");

_Static_assert(offsetof(xns_$idp_state_t, packets_received) == 0x004, "state packets_received at +0x004");
_Static_assert(offsetof(xns_$idp_state_t, packets_dropped) == 0x008, "state packets_dropped at +0x008");
_Static_assert(offsetof(xns_$idp_state_t, addrs) == 0x020, "state addrs at +0x020");
_Static_assert(offsetof(xns_$idp_state_t, ports) == 0x040, "state ports at +0x040");
_Static_assert(offsetof(xns_$idp_state_t, channels) == 0x0A0, "state channels at +0x0A0");
_Static_assert(offsetof(xns_$idp_state_t, lock) == 0x520, "state lock at +0x520");
_Static_assert(offsetof(xns_$idp_state_t, open_channels) == 0x534, "state open_channels at +0x534");
_Static_assert(offsetof(xns_$idp_state_t, next_socket) == 0x536, "state next_socket at +0x536");
_Static_assert(offsetof(xns_$idp_state_t, registered_count) == 0x538, "state registered_count at +0x538");
#endif /* ARCH_M68K */


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
  mac_os_$buf_desc_t desc; /* 0x00: {length, address, next} - the same triple
                            * MAC_OS_$SEND walks, so `next' is a virtual
                            * address and not a host pointer */
  uint8_t flags;           /* 0x0C: cleared by XNS_IDP_$SEND at 0x00E18B36 */
  uint8_t _pad_0d[3];      /* 0x0D */
} xns_$idp_iov_t;

#if defined(ARCH_M68K)
_Static_assert(offsetof(xns_$idp_iov_t, desc)  == 0x00, "idp_iov.desc");
_Static_assert(offsetof(xns_$idp_iov_t, flags) == 0x0C, "idp_iov.flags");
#endif

/*
 * xns_$idp_send_t - the request record XNS_IDP_$SEND (0x00E18A66) is given.
 *
 * Everything XNS_IDP_$SEND reads out of its second argument, in order:
 *   0x00  24 bytes copied verbatim into its own request record with
 *         "moveq #0x17,D1 / move.b (A0)+,(A1)+ / dbf" at 0x00E18B04, i.e. the
 *         destination address followed by the source address (see
 *         xns_$os_send_rec_t)
 *   0x18  "cmpi.l #0x1e,(0x18,A0)" at 0x00E18ADE - rejected below the
 *         30-byte IDP header size - and then the whole {length, address,
 *         next} descriptor is copied at 0x00E18B1E-0x00E18B2A
 *   0x1C  "tst.l (0x1c,A0)" at 0x00E18AD8 - rejected when zero
 *   0x20  the head of the caller's buffer chain, walked at
 *         0x00E18B30-0x00E18B42 clearing each entry's +0x0C flag byte
 *   0x2C  "move.w (0x2c,A0),(-0x1c,A6)" at 0x00E18B10 - a WORD; only its low
 *         byte (+0x2D) is used, as the IDP packet type
 *
 * Nothing else is read, so the size of the user's record is unknown; the
 * tail is padded out to the 0x48 bytes of the record built for
 * XNS_IDP_$OS_SEND.
 */
typedef struct xns_$idp_send_t {
  xns_$net_addr_t dest_addr;      /* 0x00: destination network/host/socket */
  xns_$net_addr_t src_addr;       /* 0x0C: source network/host/socket */
  mac_os_$buf_desc_t hdr_desc;    /* 0x18: {length, address, next} */
  uint8_t _unknown_24[8];         /* 0x24: not read by XNS_IDP_$SEND */
  uint16_t packet_type;           /* 0x2C: low byte is the IDP packet type */
  uint8_t _unknown_2e[0x1A];      /* 0x2E: not read by XNS_IDP_$SEND */
} __attribute__((packed)) xns_$idp_send_t;

#if defined(ARCH_M68K)
_Static_assert(offsetof(xns_$idp_send_t, dest_addr)   == 0x00, "idp_send.dest_addr");
_Static_assert(offsetof(xns_$idp_send_t, src_addr)    == 0x0C, "idp_send.src_addr");
_Static_assert(offsetof(xns_$idp_send_t, hdr_desc)    == 0x18, "idp_send.hdr_desc");
_Static_assert(offsetof(xns_$idp_send_t, packet_type) == 0x2C, "idp_send.packet_type");
_Static_assert(sizeof(xns_$idp_send_t) == 0x48, "xns_$idp_send_t must be 0x48 bytes");
#endif

/*
 * Status codes for XNS IDP operations
 */
/*
 * Status codes for XNS IDP operations (module 0x3B, "OS / XNS IDP").
 *
 * The comment after each line is the exact text the 10.4 status-code
 * database (module 0x3B, "OS / XNS IDP") gives for that code, and every
 * identifier now agrees with it (bead source-v1lr; the eight that did not -
 * 0x3B0002, 0x0008, 0x000B, 0x000D, 0x0010, 0x0013, 0x001A and 0x001B - were
 * renamed, values unchanged).
 */
#define status_$xns_channel_table_full               0x3B0001  /* no channels available */
#define status_$xns_no_os_sockets                    0x3B0002  /* no OS sockets available */
#define status_$xns_no_demux                         0x3B0003  /* caller specified neither OS socket nor demux proc */
#define status_$xns_bad_channel                      0x3B0004  /* channel is not open */
#define status_$xns_no_socket                        0x3B0005  /* no socket allocated for caller */
#define status_$xns_no_data                          0x3B0006  /* no packet available to receive */
#define status_$xns_buffer_too_small                 0x3B0007  /* data capacity too small for received packet */
#define status_$xns_illegal_buffer_spec              0x3B0008  /* illegal buffer specification */
#define status_$xns_addr_in_use                      0x3B0009  /* address in use */
#define status_$xns_invalid_type_count               0x3B000A  /* invalid type count */
#define status_$xns_listen_network_not_connected     0x3B000B  /* listen network not connected */
#define status_$xns_reserved_socket                  0x3B000C  /* illegal IDP socket */
#define status_$xns_idp_socket_table_full            0x3B000D  /* IDP socket table full */
#define status_$xns_socket_in_use                    0x3B000E  /* IDP socket in use */
#define status_$xns_os_socket_not_open               0x3B000F  /* OS socket not open */
#define status_$xns_no_client_for_packet             0x3B0010  /* no client for packet */
#define status_$xns_bad_checksum                     0x3B0011  /* bad IDP checksum */
#define status_$xns_hop_count_exceeded               0x3B0012  /* maximum hops exceeded by packet */
#define status_$xns_network_unreachable              0x3B0013  /* network unreachable */
#define status_$xns_illegal_os_socket                0x3B0014  /* illegal OS socket */
#define status_$xns_version_mismatch                 0x3B0015  /* invalid version number */
#define status_$xns_could_not_put_packet_into_socket  0x3B0016  /* could not put packet into socket */
#define status_$xns_no_buffer_size                   0x3B0017  /* no OS socket depth given */
#define status_$xns_incompatible_flags               0x3B0018  /* cannot send only as well as listen */
#define status_$xns_incompatible_flags2              0x3B0019  /* cannot send only as well as connect */
#define status_$xns_connect_to_broadcast             0x3B001A  /* cannot connect to broadcast address */
#define status_$xns_source_must_be_this_node         0x3B001B  /* connection source address must be this node */
#define status_$xns_connect_bind_conflict            0x3B001C  /* cannot connect as well as listen */
#define status_$xns_too_many_addrs                   0x3B001D  /* host address table full */

/*
 * XNS Error Protocol codes (param to XNS_ERROR_$SEND)
 */
/* Errors detected at the destination host (0x00xx) */
#define XNS_ERROR_UNSPEC 0x0000       /* Unspecified error */
#define XNS_ERROR_BAD_CHECKSUM 0x0001 /* Bad checksum (cell at 0x00E1872A) */
#define XNS_ERROR_NO_SOCKET 0x0002    /* No socket listening */
#define XNS_ERROR_RESOURCE 0x0003     /* Resource exhausted */

/* Errors detected in transit, before the packet reached its destination
 * (0x02xx).  XNS_IDP_$OS_DEMUX picks between XNS_ERROR_BAD_CHECKSUM and
 * XNS_ERROR_BAD_CHECKSUM_TRANSIT at 0x00E1853E depending on whether the
 * IDP destination address is one of ours. */
#define XNS_ERROR_UNSPEC_TRANSIT 0x0200       /* Unspecified error in transit */
#define XNS_ERROR_BAD_CHECKSUM_TRANSIT 0x0201 /* Bad checksum (cell at 0x00E18728) */
#define XNS_ERROR_UNREACHABLE 0x0202          /* Destination unreachable */
#define XNS_ERROR_TOO_MANY_HOPS 0x0203        /* Hop count exceeded */
#define XNS_ERROR_TOO_LARGE 0x0204            /* Packet too large */

/* The error-parameter cell at 0x00E18726 is a plain zero word. */
#define XNS_ERROR_PARAM_NONE 0x0000

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
 * @param len_sent_ret  Output: the word XNS_IDP_$OS_SEND returned
 *                      (0x00E18B5C-0x00E18B64)
 * @param status_ret    Output: status code
 *
 * Original address: 0x00E18A66
 */
void XNS_IDP_$SEND(uint16_t *channel, xns_$idp_send_t *send_params,
                   int16_t *len_sent_ret, status_$t *status_ret);

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
 * xns_$os_send_rec_t - the 0x48-byte request record XNS_IDP_$OS_SEND is given
 *
 * XNS_IDP_$OS_SEND (0x00E18256) reads its second argument as follows:
 *   0x00  24 bytes copied into the IDP header at header +0x06 with
 *         "lea (0x6,A1),A4 / moveq #0x17,D1 / move.b (A3)+,(A4)+ / dbf" at
 *         0x00E18336-0x00E1833E, taken only when the channel asks for a
 *         header build and is NOT connected.  The connected alternative at
 *         0x00E18318-0x00E18332 writes those same 24 header bytes from the
 *         channel's dest_network (chan +0xA4, twelve bytes) and src_network
 *         (chan +0xB0, twelve bytes), which is what identifies the range:
 *         a destination xns_$net_addr_t followed by a source one.
 *         (source-2ptk)
 *   0x18  "move.l (0x18,A3),D1"  at 0x00E182C8 - the first buffer length, and
 *         the running total it accumulates while walking the chain
 *   0x1C  "move.l (0x1c,A1),D3"  at 0x00E1828E - the IDP header buffer; the
 *         12-byte destination address is read from that buffer + 6
 *         (0x00E18364) and handed to RIP_$FIND_NEXTHOP
 *   0x20  "movea.l (0x20,A3),A0" at 0x00E182C4 - the next buffer descriptor
 *   0x18..0x23 are copied straight into mac_os_$send_pkt_t.hdr_desc
 *         (0x00E18408-0x00E18414), so they are the same {length, address,
 *         next} triple MAC_OS_$SEND walks
 *   0x24  "move.b (0x24,A1),(-0x60,A6)" at 0x00E18416 - becomes the MAC
 *         record's hdr_prebuilt boolean
 *   0x2D  "move.b (0x2d,A3),(0x5,A1)" at 0x00E1830E - the IDP packet type.
 *         XNS_IDP_$SEND writes the containing word at +0x2C
 *         (0x00E18B10), so the field is modelled as a word.
 *   0x34  five longwords copied to the MAC record's +0x38
 *         (0x00E1841C-0x00E18428): the payload length followed by the four
 *         payload page addresses.  Its low word is also added to the IDP
 *         length ("add.w (0x36,A3),D1w" at 0x00E18302).
 *
 * The record is therefore a mac_os_$send_pkt_t shifted down by four bytes
 * from +0x18 on.  0x00..0x17 is not written on the RIP_$SEND path (whose
 * copy lives at A6-0x48 and whose channel builds its own IDP header) nor on
 * the XNS_ERROR_$SEND path (record at 0x00E2B29C); XNS_IDP_$SEND fills it
 * from the user's record at 0x00E18B04.
 */
typedef struct xns_$os_send_rec_t {
    xns_$net_addr_t     dest_addr;      /* 0x00: destination network/host/socket */
    xns_$net_addr_t     src_addr;       /* 0x0C: source network/host/socket */
    mac_os_$buf_desc_t  hdr_desc;       /* 0x18: {length, address, next} */
    int8_t      hdr_prebuilt;           /* 0x24: Pascal boolean, 0xFF = true */
    uint8_t     _pad_25[3];             /* 0x25 */
    uint8_t     _unknown_28[4];         /* 0x28 */
    uint16_t    packet_type;            /* 0x2C: low byte is the IDP packet type */
    uint8_t     _unknown_2e[6];         /* 0x2E */
    uint32_t    data_length;            /* 0x34 */
    uint32_t    data_pages[4];          /* 0x38 */
} __attribute__((packed)) xns_$os_send_rec_t;

#if defined(ARCH_M68K)
_Static_assert(offsetof(xns_$os_send_rec_t, dest_addr)    == 0x00, "os_send_rec.dest_addr");
_Static_assert(offsetof(xns_$os_send_rec_t, src_addr)     == 0x0C, "os_send_rec.src_addr");
_Static_assert(offsetof(xns_$os_send_rec_t, hdr_desc)     == 0x18, "os_send_rec.hdr_desc");
_Static_assert(offsetof(xns_$os_send_rec_t, hdr_prebuilt) == 0x24, "os_send_rec.hdr_prebuilt");
_Static_assert(offsetof(xns_$os_send_rec_t, packet_type)  == 0x2C, "os_send_rec.packet_type");
_Static_assert(offsetof(xns_$os_send_rec_t, data_length)  == 0x34, "os_send_rec.data_length");
_Static_assert(offsetof(xns_$os_send_rec_t, data_pages)   == 0x38, "os_send_rec.data_pages");
_Static_assert(sizeof(xns_$os_send_rec_t) == 0x48, "xns_$os_send_rec_t must be 0x48 bytes");
#endif

/*
 * The MAC frame type XNS_IDP_$OS_SEND stamps into mac_os_$send_pkt_t.frame_type
 * ("move.l #0x600,(-0x58,A6)" at 0x00E183FC).  ROUTE_$PROCESS uses the same
 * value at 0x00E876BA.
 */
#define XNS_MAC_FRAME_TYPE 0x600

/*
 * XNS_IDP_$OS_SEND - Send a packet (OS-level)
 *
 * @param channel       Pointer to channel number (read as a word)
 * @param send_rec      The request record
 * @param len_sent_ret  Output: the word MAC_OS_$SEND reports (cleared at
 *                      0x00E18268, handed straight to MAC_OS_$SEND's third
 *                      argument at 0x00E18460)
 * @param status_ret    Output: status code
 *
 * Original address: 0x00E18256
 */
void XNS_IDP_$OS_SEND(int16_t *channel, xns_$os_send_rec_t *send_rec,
                      int16_t *len_sent_ret, status_$t *status_ret);

/*
 * XNS_IDP_$OS_DEMUX - Demultiplex incoming packet (OS-level)
 *
 * Called by MAC layer when an IDP packet arrives.
 *
 * @param pkt           MAC-layer receive descriptor
 * @param port_ptr       Pointer to the ROUTE port number the frame arrived on
 * @param mac_broadcast  Pointer to a Domain boolean: the frame was received
 *                       as a MAC-level broadcast/multicast
 * @param status_ret     Output: status code
 *
 * Original address: 0x00E184A8
 */
void XNS_IDP_$OS_DEMUX(xns_$mac_rcv_t *pkt, int16_t *port_ptr,
                       boolean *mac_broadcast, status_$t *status_ret);

/*
 * Type of a channel's demux vector (xns_$channel_t.demux).
 *
 * Called at 0x00E18658 with five longword arguments.
 */
typedef void (*xns_$demux_fn_t)(xns_$pkt_desc_t *rec, uint16_t *port_type,
                                uint16_t *port_socket, boolean *mac_broadcast,
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
 * @param rec            Packet descriptor built by XNS_IDP_$OS_DEMUX
 * @param port_type      Pointer to route_$port_t.port_type   (rport +0x2E)
 * @param port_socket    Pointer to route_$port_t.socket      (rport +0x30)
 * @param mac_broadcast  Pointer to a Domain boolean (see XNS_IDP_$OS_DEMUX)
 * @param status_ret     Output: status code
 *
 * Original address: 0x00E18B8A
 */
void XNS_IDP_$DEMUX(xns_$pkt_desc_t *rec, uint16_t *port_type,
                    uint16_t *port_socket, boolean *mac_broadcast,
                    status_$t *status_ret);

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

/*
 * =============================================================================
 * Layout checks and accessors added for ROUTE_$PROCESS (see route/process.c)
 * =============================================================================
 */

/*
 * xns_$idp_header_t must lay out exactly as the wire header: ROUTE_$PROCESS
 * bumps transport_ctl with addq.b #1,(0x4,A2) (0x00E87526), tests the
 * checksum with cmpi.w #-1,(A2) (0x00E8752A) and copies the 12-byte
 * destination address starting at idp+6 (lea (0x6,A2),A0 at 0x00E87564).
 */
#if defined(ARCH_M68K)
_Static_assert(offsetof(xns_$idp_header_t, checksum)      == 0x00, "idp.checksum");
_Static_assert(offsetof(xns_$idp_header_t, length)        == 0x02, "idp.length");
_Static_assert(offsetof(xns_$idp_header_t, transport_ctl) == 0x04, "idp.transport_ctl");
_Static_assert(offsetof(xns_$idp_header_t, packet_type)   == 0x05, "idp.packet_type");
_Static_assert(offsetof(xns_$idp_header_t, dest_network)  == 0x06, "idp.dest_network");
_Static_assert(offsetof(xns_$idp_header_t, dest_host)     == 0x0A, "idp.dest_host");
_Static_assert(offsetof(xns_$idp_header_t, dest_socket)   == 0x10, "idp.dest_socket");
_Static_assert(offsetof(xns_$idp_header_t, src_network)   == 0x12, "idp.src_network");
_Static_assert(offsetof(xns_$idp_header_t, src_socket)    == 0x1C, "idp.src_socket");
_Static_assert(sizeof(xns_$idp_header_t) == 0x1E, "xns_$idp_header_t must be 30 bytes");
#endif

/*
 * XNS_IDP_$PORT_MAC_CHANNEL - address of the MAC channel word for a port
 *
 * The IDP state block carries an eight-entry, twelve-byte-per-entry port
 * table at XNS_$IDP_STATE + 0x40 (0xE2B354).  Its third field, at +0x08 of
 * an entry, is the MAC_OS channel number; both XNS_IDP_$SEND (0x00E18474,
 * pea (0x48,A5,D1) with D1 = port*12) and ROUTE_$PROCESS (0x00E876FA,
 * pea (0x8,A0,D1) with A0 = 0xE2B354) pass its address as MAC_OS_$SEND's
 * first argument.
 */
#define XNS_IDP_PORT_ENTRY_SIZE     12
#define XNS_IDP_PORT_TABLE_OFFSET   0x40
#define XNS_IDP_PORT_MAC_CHANNEL_OFFSET 0x08

#define XNS_IDP_$PORT_MAC_CHANNEL(port)                                       \
    ((int16_t *)((uint8_t *)XNS_$IDP_STATE + XNS_IDP_PORT_TABLE_OFFSET +      \
                 (uint32_t)(port) * XNS_IDP_PORT_ENTRY_SIZE +                 \
                 XNS_IDP_PORT_MAC_CHANNEL_OFFSET))

#endif /* XNS_H */
