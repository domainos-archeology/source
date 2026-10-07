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
 * Module data: XNS_IDP_$DATA (image 0xE2B314, below) and XNS_ERROR_$DATA
 * (image 0xE2B29C, xns/xns_internal.h), MODULE_DATA blocks since
 * source-iq58 (Claude Opus 5.5); definitions in xns/xns_data.c.
 */

#ifndef XNS_H
#define XNS_H

#include "base/base.h"
#include "arch/arch.h"
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
 * ============================================================================
 * XNS_IDP_$DATA - the XNS_IDP module block, 0x00E2B314..0x00E2B84F
 * ============================================================================
 *
 * SAU2 map "D E2B314 XNS_IDP size = 53C", interior symbol
 * XNS_IDP_$PORT_TABLE at 0xE2B354 (+0x40, the ports[] array).  Every IDP
 * routine loads "lea (0xe2b314).l,A5" (XNS_IDP_$OS_OPEN .. REGISTER_ADDR;
 * XNS_IDP_$INIT uses "movea.l #0xe2b314,A0"), so each (off,A5) is a field.
 * The address is the block's image address - the ordering key of
 * tools/gen_layout_ld.py and documentation, not where it is linked
 * (docs/design-per-process-data.md).  All 0x53C bytes are zero in the image;
 * XNS_IDP_$INIT fills the block at boot.
 *
 * The three tables are 0-based: the loops start their index at 0
 * (xns_$find_socket, XNS_IDP_$OS_OPEN "clr.w D2w" 0x00E17F56,
 * XNS_IDP_$REGISTER_ADDR "clr.w D1w" 0x00E1902A, XNS_IDP_$INIT "moveq
 * #0xf,D0 / movea.l A0,A1") and index from the table's own first element:
 *   addr_port[i]  (0x10,A0), A0 = A5 + i*2         0x00E1902E
 *   addrs[i]      (0x20,A5,D0*1), D0 = i*6         0x00E19044
 *   ports[p]      (0x40..0x4A,A5+p*0xC)            0x00E17C30, 0x00E3031E
 *   channels[c]   (0xA0..0xE4,A5+c*0x48)           0x00E30296, 0x00E17F72
 * registered_count is the dbf count of addrs[] (entries 0..count are in
 * use), which is why XNS_IDP_$REGISTER_ADDR appends at (0x26,A5,count*6) /
 * (0x12,A5,count*2), i.e. element count + 1.
 *
 * The channels carry a code pointer (the demux vector) and the lock native
 * pointers, so the offsets from +0xA0 on and the size are asserted on the
 * target only; everything before is pointer-free and asserted everywhere.
 */
#define XNS_IDP_$DATA_SIZE 0x53C        /* map: XNS_IDP size = 53C */

typedef struct xns_$idp_data_t {
  /* Statistics, returned by XNS_IDP_$GET_STATS (0x00E18FE6-0x00E18FEE) */
  uint32_t packets_sent;     /* +0x000: bumped by XNS_IDP_$OS_SEND */
  uint32_t packets_received; /* +0x004: "addq.l #1,(0x4,A5)" 0x00E184C2 */
  uint32_t packets_dropped;  /* +0x008: "addq.l #1,(0x8,A5)" 0x00E184E2 */

  uint8_t _unknown_0c[4];    /* +0x00C: not referenced */

  /*
   * +0x010: the ROUTE port each registered address belongs to, one word per
   * addrs[] entry; XNS_IDP_$INIT sets entry 0 (the node's own address) to
   * -1, any port ("move.w #-0x1,(0x10,A0)" 0x00E3030C).
   */
  int16_t addr_port[XNS_MAX_ADDRS];

  uint8_t _unknown_18[8];    /* +0x018: not referenced */

  /*
   * +0x020: the registered local host addresses, three words each (every
   * access is a word move: 0x00E302E6-0x00E30304, 0x00E19044-0x00E1904E,
   * xns_$is_broadcast_addr's (0x20,A1)/(0x22,A1)/(0x24,A1)).  Entry 0 is
   * the node's own address, built by XNS_IDP_$INIT from NODE_$ME.
   */
  uint16_t addrs[XNS_MAX_ADDRS][3];

  uint8_t _unknown_38[8];    /* +0x038: not referenced */

  /* +0x040 map XNS_IDP_$PORT_TABLE: per ROUTE port state, stride 0x0C */
  xns_$port_state_t ports[XNS_MAX_PORTS];

  /* +0x0A0: the channels, stride 0x48 (16 * 0x48 ends at the lock) */
  xns_$channel_t channels[XNS_MAX_CHANNELS];

  /* +0x520: the module's exclusion lock ("pea (0x520,A5)"); 0x12 bytes */
  ml_$exclusion_t lock;
  uint8_t _unknown_532[2];   /* +0x532: not referenced */

  uint16_t open_channels;    /* +0x534: open channel count (unsigned compare
                              * "cmpi.w #0x10,(0x534,A5)" / bcs 0x00E17F1A) */
  uint16_t next_socket;      /* +0x536: next dynamic socket (from 0xBB9) */
  int16_t  registered_count; /* +0x538: dbf count of addrs[] entries */
  uint8_t _unknown_53a[2];   /* +0x53A: not referenced */
} xns_$idp_data_t;

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
_Static_assert(sizeof(xns_$port_state_t) == 0x0C, "xns_$port_state_t is 0x0C bytes");
_Static_assert(offsetof(xns_$port_state_t, mac_socket) == 0x08, "port mac_socket at +0x08");

_Static_assert(offsetof(xns_$idp_data_t, packets_received) == 0x004, "packets_received at +0x004");
_Static_assert(offsetof(xns_$idp_data_t, packets_dropped) == 0x008, "packets_dropped at +0x008");
_Static_assert(offsetof(xns_$idp_data_t, addr_port) == 0x010, "addr_port at +0x010");
_Static_assert(offsetof(xns_$idp_data_t, addrs) == 0x020, "addrs at +0x020");
_Static_assert(offsetof(xns_$idp_data_t, ports) == 0x040, "ports (XNS_IDP_$PORT_TABLE) at +0x040");
_Static_assert(offsetof(xns_$idp_data_t, channels) == 0x0A0, "channels at +0x0A0");
_Static_assert(sizeof(((xns_$idp_data_t *)0)->addr_port[0]) == 2, "addr_port stride (add.w D0,D0)");
_Static_assert(sizeof(((xns_$idp_data_t *)0)->addrs[0]) == 6, "addrs stride (i*6)");
_Static_assert(offsetof(xns_$idp_data_t, addrs[XNS_MAX_ADDRS]) == 0x038, "addrs[3] ends at +0x038");
_Static_assert(offsetof(xns_$idp_data_t, ports[XNS_MAX_PORTS]) == 0x0A0,
               "ports[7] ends where the channels begin");

/*
 * The channel record carries the demux vector as a code pointer and the lock
 * two native pointers, so those layouts hold on the 32-bit target only.
 */
#if defined(ARCH_M68K)
_Static_assert(sizeof(xns_$channel_t) == 0x48, "xns_$channel_t is 0x48 bytes");
_Static_assert(offsetof(xns_$channel_t, demux) == 0x00, "channel demux at +0x00");
_Static_assert(offsetof(xns_$channel_t, connected_port) == 0x34, "channel connected_port at +0x34");
_Static_assert(offsetof(xns_$channel_t, user_socket) == 0x36, "channel user_socket at +0x36");
_Static_assert(offsetof(xns_$channel_t, xns_socket) == 0x38, "channel xns_socket at +0x38");
_Static_assert(offsetof(xns_$channel_t, flags) == 0x3A, "channel flags at +0x3A");
_Static_assert(offsetof(xns_$channel_t, port_active) == 0x3C, "channel port_active at +0x3C");
_Static_assert(offsetof(xns_$channel_t, state) == 0x44, "channel state at +0x44");

_Static_assert(sizeof(xns_$idp_data_t) == XNS_IDP_$DATA_SIZE, "XNS_IDP block: map size 0x53C");
_Static_assert(offsetof(xns_$idp_data_t, channels[XNS_MAX_CHANNELS]) == 0x520,
               "channels[15] ends at the lock");
_Static_assert(offsetof(xns_$idp_data_t, lock) == 0x520, "lock at +0x520");
_Static_assert(offsetof(xns_$idp_data_t, open_channels) == 0x534, "open_channels at +0x534");
_Static_assert(offsetof(xns_$idp_data_t, next_socket) == 0x536, "next_socket at +0x536");
_Static_assert(offsetof(xns_$idp_data_t, registered_count) == 0x538, "registered_count at +0x538");
#endif /* ARCH_M68K */

MODULE_DATA_DECLARE(xns_$idp_data_t, XNS_IDP_$DATA, 0x00E2B314);


/*
 * XNS IDP Statistics (returned by XNS_IDP_$GET_STATS)
 */
typedef struct xns_$idp_stats_t {
  uint32_t packets_sent;     /* Total packets sent */
  uint32_t packets_received; /* Total packets received */
  uint32_t packets_dropped;  /* Total packets dropped */
} xns_$idp_stats_t;

/*
 * XNS IDP Open Options - the record XNS_IDP_$OPEN (0x00E187AC) is handed.
 *
 * Every offset below is a displacement off A2 in that routine:
 *   0x00  "cmpi.w #0x1,(A2)"                 0x00E187C4  version, must be 1
 *   0x02  "move.w (0x2,A2),D0w"              0x00E187D4  the XNS socket
 *   0x04  "move.l (0x4,A2),(-0x20,A6)"       0x00E18914  listen network,
 *         copied into the OS record's +0x08 only when the bind flag is set;
 *         "move.l A0,(0x4,A2)"               0x00E189B6  overwritten on the
 *         way out with EC2_$REGISTER_EC1's result
 *   0x08  24 bytes copied to the OS record's +0x0C when the connect flag is
 *         set ("lea (0x8,A2),A0 / lea (-0x1c,A6),A1 / moveq #0x17,D0 /
 *         move.b (A0)+,(A1)+ / dbf" 0x00E18922-0x00E1892E), i.e. the SOURCE
 *         address followed by the DESTINATION address;
 *         "move.w (-0x26,A6),(0x8,A2)"       0x00E18992  the first WORD of
 *         that block is overwritten on the way out with the channel index
 *   0x0C..0x11 "cmp.w (0x10,A2) / (0xc,A2) / (0xe,A2)"  0x00E18876-0x00E18886
 *         the source host, rejected when it is all ones
 *   0x18..0x1D "cmp.w (0x1c,A2) / (0x18,A2) / (0x1a,A2)" 0x00E18864-0x00E18874
 *         the destination host, rejected when it is all ones
 *   0x20  "move.w (0x20,A2),(-0x26,A6)"      0x00E18906  the whole WORD goes
 *         to the OS record's +0x02; its LOW byte is the open-flag byte that
 *         XNS_IDP_$OS_OPEN reads back as "btst.b #n,(0x3,A1)", so the two
 *         halves are modelled separately
 *   0x21  "btst.b #0x1,(0x21,A2)"            0x00E1881A  the open flags
 *   0x22  "tst.w (0x22,A2)"                  0x00E188A0  the socket depth
 *         handed to SOCK_$ALLOCATE_USER
 *
 * PACKED so the host build reproduces the m68k layout (m68k aligns 32-bit
 * scalars on two-byte boundaries).
 */
typedef struct xns_$idp_open_opt_t {
  int16_t  version;          /* 0x00: version, must be 1 */
  int16_t  socket;           /* 0x02: XNS socket number (0 = assign one) */
  uint32_t network;          /* 0x04: IN  the network to listen on;
                              *       OUT EC2_$REGISTER_EC1's result */
  int16_t  channel_ret;      /* 0x08: OUT the channel index (0x00E18992).
                              *       IN  the high half of src_network */
  uint16_t src_network_lo;   /* 0x0A */
  uint16_t src_host_hi;      /* 0x0C */
  uint16_t src_host_mid;     /* 0x0E */
  uint16_t src_host_lo;      /* 0x10 */
  uint16_t src_socket;       /* 0x12 */
  uint32_t dest_network;     /* 0x14 */
  uint16_t dest_host_hi;     /* 0x18 */
  uint16_t dest_host_mid;    /* 0x1A */
  uint16_t dest_host_lo;     /* 0x1C */
  uint16_t dest_socket;      /* 0x1E */
  uint8_t  flags_hi;         /* 0x20: high half of the flag word the OS record
                              *       is given; never examined by either
                              *       routine */
  uint8_t  flags;            /* 0x21: open flags (XNS_OPEN_FLAG_*) */
  int16_t  buffer_size;      /* 0x22: OS socket depth */
} xns_$idp_open_opt_t;
/*
 * NOT packed: the natural layout already reproduces every offset above (the
 * _Static_asserts that follow prove it), and the caller supplies this record -
 * taking the address of one of its fields must not draw
 * -Waddress-of-packed-member.
 */

_Static_assert(offsetof(xns_$idp_open_opt_t, network)     == 0x04, "idp_open_opt.network");
_Static_assert(offsetof(xns_$idp_open_opt_t, channel_ret) == 0x08, "idp_open_opt.channel_ret");
_Static_assert(offsetof(xns_$idp_open_opt_t, src_host_hi) == 0x0C, "idp_open_opt.src_host_hi");
_Static_assert(offsetof(xns_$idp_open_opt_t, dest_network)== 0x14, "idp_open_opt.dest_network");
_Static_assert(offsetof(xns_$idp_open_opt_t, dest_host_hi)== 0x18, "idp_open_opt.dest_host_hi");
_Static_assert(offsetof(xns_$idp_open_opt_t, flags_hi)    == 0x20, "idp_open_opt.flags_hi");
_Static_assert(offsetof(xns_$idp_open_opt_t, flags)       == 0x21, "idp_open_opt.flags");
_Static_assert(offsetof(xns_$idp_open_opt_t, buffer_size) == 0x22, "idp_open_opt.buffer_size");
_Static_assert(sizeof(xns_$idp_open_opt_t) == 0x24, "xns_$idp_open_opt_t must be 0x24 bytes");

/*
 * The 24 bytes at +0x08 are one contiguous block, and so are the 24 bytes at
 * the OS record's +0x0C; the byte copy at 0x00E18922 moves one onto the
 * other.
 */
_Static_assert(offsetof(xns_$idp_open_opt_t, dest_socket) + 2 -
               offsetof(xns_$idp_open_opt_t, channel_ret) == 24,
               "the connect block is 24 bytes");

/*
 * XNS IDP OS-level Open Options - the record XNS_IDP_$OS_OPEN (0x00E17F02)
 * is handed, and the record XNS_IDP_$OPEN builds at A6-0x28 for it.
 *
 * Displacements off D6/A1 in XNS_IDP_$OS_OPEN:
 *   0x00  "tst.w (A0)"                    0x00E17F2E  the requested socket,
 *         written back with the allocated one at 0x00E18128
 *   0x02  "move.w D2w,(0x2,A0)"           0x00E181A4  OUT the channel index.
 *         IN the flag word whose low byte is read as
 *         "btst.b #0x1,(0x3,A1)"          0x00E17F7A
 *   0x04  "move.l (0x4,A1),(0xa0,A0)"     0x00E18174  the channel's demux
 *         vector, copied as a LONGWORD
 *   0x08  "cmpi.l #-0x1,(0x8,A1)"         0x00E17F82  the network to listen
 *         on, -1 meaning "every port"
 *   0x0C  the SOURCE address: all-zero test at 0x00E18008-0x00E18024 and
 *         "pea (0xc,A1)" into xns_$is_broadcast_addr at 0x00E1802A
 *   0x18  the DESTINATION address: "pea (0x18,A1)" into RIP_$FIND_NEXTHOP at
 *         0x00E18052 and copied to the channel's +0xA4 at 0x00E180C4
 */
typedef struct xns_$os_open_opt_t {
  int16_t  socket;           /* 0x00: IN/OUT XNS socket number */
  uint16_t flags_channel;    /* 0x02: IN the flag word (low byte = the flags);
                              *       OUT the channel index */
  uint32_t demux;            /* 0x04: the demux vector, a code ADDRESS moved
                              *       as one longword into the channel */
  uint32_t network;          /* 0x08: network to listen on (-1 = all ports) */
  uint32_t src_network;      /* 0x0C */
  uint16_t src_host_hi;      /* 0x10 */
  uint16_t src_host_mid;     /* 0x12 */
  uint16_t src_host_lo;      /* 0x14 */
  uint16_t src_socket;       /* 0x16 */
  uint32_t dest_network;     /* 0x18 */
  uint16_t dest_host_hi;     /* 0x1C */
  uint16_t dest_host_mid;    /* 0x1E */
  uint16_t dest_host_lo;     /* 0x20 */
  uint16_t dest_socket;      /* 0x22 */
} xns_$os_open_opt_t;
/*
 * NOT packed: the natural layout already reproduces every offset above (the
 * _Static_asserts that follow prove it), and XNS_IDP_$OPEN builds this record at A6-0x28 -
 * taking the address of one of its fields must not draw
 * -Waddress-of-packed-member.
 */

_Static_assert(offsetof(xns_$os_open_opt_t, flags_channel) == 0x02, "os_open_opt.flags_channel");
_Static_assert(offsetof(xns_$os_open_opt_t, demux)         == 0x04, "os_open_opt.demux");
_Static_assert(offsetof(xns_$os_open_opt_t, network)       == 0x08, "os_open_opt.network");
_Static_assert(offsetof(xns_$os_open_opt_t, src_network)   == 0x0C, "os_open_opt.src_network");
_Static_assert(offsetof(xns_$os_open_opt_t, src_socket)    == 0x16, "os_open_opt.src_socket");
_Static_assert(offsetof(xns_$os_open_opt_t, dest_network)  == 0x18, "os_open_opt.dest_network");
_Static_assert(offsetof(xns_$os_open_opt_t, dest_socket)   == 0x22, "os_open_opt.dest_socket");
_Static_assert(sizeof(xns_$os_open_opt_t) == 0x24, "xns_$os_open_opt_t must be 0x24 bytes");
_Static_assert(offsetof(xns_$os_open_opt_t, dest_socket) + 2 -
               offsetof(xns_$os_open_opt_t, src_network) == 24,
               "the connect block is 24 bytes");

/* Open flags - the byte at xns_$idp_open_opt_t +0x21 / the low half of
 * xns_$os_open_opt_t.flags_channel. */
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

_Static_assert(offsetof(xns_$idp_iov_t, desc)  == 0x00, "idp_iov.desc");
_Static_assert(offsetof(xns_$idp_iov_t, flags) == 0x0C, "idp_iov.flags");

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

/*
 * xns_$idp_recv_t - the record XNS_IDP_$RECEIVE (0x00E18CE2) is given
 *
 * Displacements off A3 in that routine:
 *   0x00  24 bytes written from the received IDP header + 6, i.e. the
 *         destination address followed by the source address, when the
 *         channel asked for a header build ("lea (0x6,A0),A1 /
 *         movea.l (0xc,A6),A3 / moveq #0x17,D1 / move.b (A1)+,(A3)+ / dbf"
 *         at 0x00E18D7C-0x00E18D8A)
 *   0x18  the head of the caller's buffer chain, and the descriptor whose
 *         +0x1C (its address) must be non-zero ("tst.l (0x1c,A3)" at
 *         0x00E18DC8).  The chain is walked through each node's +0x08 and
 *         every unused node's length is zeroed on the way out
 *         (0x00E18E8C-0x00E18EB4)
 *   0x26  "move.l (-0x3c,A6),(0x26,A3)"  0x00E18D9E  sock_$pkt_info_t.src_addr
 *   0x2A  "move.w (-0x38,A6),(0x2a,A3)"  0x00E18DA4  sock_$pkt_info_t.src_port
 *   0x2C  "move.w D1w,(0x2c,A3)"         0x00E18D96  the IDP packet type,
 *         zero-extended from the received header's +0x05
 *
 * The tail is padded out to the 0x48 bytes of xns_$idp_send_t, whose first
 * 0x18 bytes have the same meaning.
 */
typedef struct xns_$idp_recv_t {
  xns_$net_addr_t dest_addr;      /* 0x00: destination network/host/socket */
  xns_$net_addr_t src_addr;       /* 0x0C: source network/host/socket */
  mac_os_$buf_desc_t iov;         /* 0x18: {length, address, next} */
  uint8_t  _unknown_24[2];        /* 0x24 */
  uint32_t mac_src_hi;            /* 0x26: MAC source, high 4 bytes */
  uint16_t mac_src_lo;            /* 0x2A: MAC source, low 2 bytes */
  uint16_t packet_type;           /* 0x2C */
  uint8_t  _unknown_2e[0x1A];     /* 0x2E */
} __attribute__((packed)) xns_$idp_recv_t;

_Static_assert(offsetof(xns_$idp_recv_t, iov)         == 0x18, "idp_recv.iov");
_Static_assert(offsetof(xns_$idp_recv_t, mac_src_hi)  == 0x26, "idp_recv.mac_src_hi");
_Static_assert(offsetof(xns_$idp_recv_t, mac_src_lo)  == 0x2A, "idp_recv.mac_src_lo");
_Static_assert(offsetof(xns_$idp_recv_t, packet_type) == 0x2C, "idp_recv.packet_type");
_Static_assert(sizeof(xns_$idp_recv_t) == 0x48, "xns_$idp_recv_t must be 0x48 bytes");

_Static_assert(offsetof(xns_$idp_send_t, dest_addr)   == 0x00, "idp_send.dest_addr");
_Static_assert(offsetof(xns_$idp_send_t, src_addr)    == 0x0C, "idp_send.src_addr");
_Static_assert(offsetof(xns_$idp_send_t, hdr_desc)    == 0x18, "idp_send.hdr_desc");
_Static_assert(offsetof(xns_$idp_send_t, packet_type) == 0x2C, "idp_send.packet_type");
_Static_assert(sizeof(xns_$idp_send_t) == 0x48, "xns_$idp_send_t must be 0x48 bytes");

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
 * Status codes for the XNS Error Protocol (module 0x39, "OS / XNS Error
 * Protocol").  The comment after each line is the exact text the 10.2
 * status-code database gives for that code.  Only the first three are
 * produced by XNS_ERROR_$SEND (0x00E17A88, 0x00E17AB8, 0x00E17AD2).
 */
#define status_$xns_error_source_is_broadcast   0x390001  /* source is broadcast address */
#define status_$xns_error_illegal_buffer_spec   0x390002  /* illegal buffer specification */
#define status_$xns_error_packet_type_error     0x390003  /* packet type error */
#define status_$xns_error_cannot_open_idp       0x390004  /* cannot open to XNS IDP */

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

/*
 * xns_$error_pkt_t - the packet XNS_ERROR_$SEND builds (0x4C bytes)
 *
 * The header buffer NETBUF_$GET_HDR hands back is filled in at
 * 0x00E17B3C-0x00E17BA0, and xns_$setup_error_header (0x00E17960) has already
 * appended up to 0x2A bytes of the offending packet starting at +0x22
 * ("move.w #0x2a,(-0x36,A2)" / "moveq #0x22,D3").  0x22 + 0x2A == 0x4C, which
 * is the constant the length is computed from ("moveq #0x4c,D1 /
 * sub.l D0,D1" at 0x00E17B26).
 *
 * The address swap is done inside this record: 0x00E17B52
 * "lea (0x34,A2),A1 / lea (0x6,A0),A3" plus three "move.l (A1)+,(A3)+" copies
 * twelve bytes from +0x34 - i.e. orig[0x12], the offending packet's IDP
 * SOURCE address - onto the new header's DESTINATION address.
 */
typedef struct xns_$error_pkt_t {
  xns_$idp_header_t idp;        /* 0x00: the error packet's own IDP header */
  uint16_t error_code;          /* 0x1E: 0x00E17B9E "move.w (A3),(0x1e,A0)" */
  uint16_t error_param;         /* 0x20: 0x00E17B96 "move.w (A1),(0x20,A0)" */
  uint8_t  orig[0x2A];          /* 0x22: the head of the offending packet */
} xns_$error_pkt_t;
/*
 * NOT packed: the natural layout already reproduces every offset above (the
 * _Static_asserts that follow prove it), and the packet lives in a netbuf header page -
 * taking the address of one of its fields must not draw
 * -Waddress-of-packed-member.
 */

_Static_assert(offsetof(xns_$error_pkt_t, error_code)  == 0x1E, "error_pkt.error_code");
_Static_assert(offsetof(xns_$error_pkt_t, error_param) == 0x20, "error_pkt.error_param");
_Static_assert(offsetof(xns_$error_pkt_t, orig)        == 0x22, "error_pkt.orig");
_Static_assert(sizeof(xns_$error_pkt_t) == 0x4C, "xns_$error_pkt_t must be 0x4C bytes");

/* The offset inside orig[] of the offending packet's IDP source address:
 * record +0x34 (0x00E17B52) minus the 0x22 the copy starts at. */
#define XNS_ERROR_ORIG_SRC_ADDR_OFFSET 0x12


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
void XNS_IDP_$RECEIVE(uint16_t *channel, xns_$idp_recv_t *recv_params,
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
void XNS_IDP_$OS_OPEN(xns_$os_open_opt_t *options, status_$t *status_ret);

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

_Static_assert(offsetof(xns_$os_send_rec_t, dest_addr)    == 0x00, "os_send_rec.dest_addr");
_Static_assert(offsetof(xns_$os_send_rec_t, src_addr)     == 0x0C, "os_send_rec.src_addr");
_Static_assert(offsetof(xns_$os_send_rec_t, hdr_desc)     == 0x18, "os_send_rec.hdr_desc");
_Static_assert(offsetof(xns_$os_send_rec_t, hdr_prebuilt) == 0x24, "os_send_rec.hdr_prebuilt");
_Static_assert(offsetof(xns_$os_send_rec_t, packet_type)  == 0x2C, "os_send_rec.packet_type");
_Static_assert(offsetof(xns_$os_send_rec_t, data_length)  == 0x34, "os_send_rec.data_length");
_Static_assert(offsetof(xns_$os_send_rec_t, data_pages)   == 0x38, "os_send_rec.data_pages");
_Static_assert(sizeof(xns_$os_send_rec_t) == 0x48, "xns_$os_send_rec_t must be 0x48 bytes");

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
uint16_t XNS_IDP_$CHECKSUM(uint16_t *data, uint32_t word_count_slot);
/* Pascal frame (xns/sau2/idp_checksum.s 0xE2B850): (4) data.l, (8)
 * word_count.w; callers `subq.l #2,sp; move.w n; pea data' (call
 * 0xE17D84).  gcc slot 2 = word_count in its first word (arch/arch.h "Pascal parameter slots", source-nxtd). */
#define XNS_IDP_$CHECKSUM(data, word_count) \
    (XNS_IDP_$CHECKSUM)((data), ARCH_PASCAL_WORD_SLOT(word_count))

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
int16_t XNS_IDP_$HOP_AND_SUM(uint32_t sum_hop_slot);
/* Pascal frame (0xE2B872 `move.w (6,sp),d1' / `add.w (4,sp),d0'): (4)
 * current_sum.w, (6) hop_offset.w; the image caller (call 0xE87536)
 * pushes `move.w hop; move.w sum'.  gcc slot 1 = current_sum then
 * hop_offset (one pair slot). */
#define XNS_IDP_$HOP_AND_SUM(current_sum, hop_offset) \
    (XNS_IDP_$HOP_AND_SUM)(ARCH_PASCAL_WORD_PAIR_SLOT(current_sum, hop_offset))

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
void XNS_ERROR_$SEND(xns_$pkt_desc_t *packet_info, uint16_t *error_code,
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

/*
 * XNS_IDP_$PORT_MAC_CHANNEL - address of the MAC channel word for a port
 *
 * The IDP block's eight-entry port table (XNS_IDP_$DATA.ports, map
 * XNS_IDP_$PORT_TABLE 0xE2B354) holds each port's MAC_OS channel number at
 * +0x08 of its entry; both XNS_IDP_$OS_SEND (0x00E18474, pea (0x48,A5,D1)
 * with D1 = port*12) and ROUTE_$PROCESS (0x00E876FA, pea (0x8,A0,D1) with
 * A0 = 0xE2B354) pass its address as MAC_OS_$SEND's first argument.
 */
#define XNS_IDP_$PORT_MAC_CHANNEL(port) \
    ((int16_t *)&XNS_IDP_$DATA.ports[(port)].mac_socket)

#endif /* XNS_H */
