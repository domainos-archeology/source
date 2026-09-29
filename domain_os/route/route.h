/*
 * ROUTE - Network Routing Port Management Module
 *
 * This module provides port management and routing services for network
 * communication in Domain/OS. It manages routing ports, handles port
 * lookups, and provides event count registration for asynchronous I/O.
 *
 * The ROUTE subsystem maintains up to 8 routing ports, each with its
 * associated network configuration and socket bindings.
 *
 * Module data blocks ROUTE_$WIRED_DATA, ROUTE_$UNWIRED_DATA and
 * ROUTE_$RTWIRED_DATA: Claude Opus 5.5 (source-ybch).
 */

#ifndef ROUTE_H
#define ROUTE_H

#include "base/base.h"
#include "rip/rip.h"   /* rip_$dest_addr_t: route_$port_t.xns_addr */
#include "mac_os/mac_os.h" /* mac_os_$link_addr_t: route_$port_t.link_addr */
#include "ec/ec.h"         /* ec_$eventcount_t: ROUTE_$WIRED_DATA.control_ec */

/*
 * Port structure (0x5C = 92 bytes)
 *
 * Each network port has associated configuration including network
 * and socket identifiers. The system supports up to 8 ports.
 *
 * Layout (partially decoded):
 *   +0x00: network address (4 bytes)
 *   +0x2C: active status (2 bytes) - non-zero if port active
 *   +0x2E: port type/network (2 bytes) - 1=local, 2=routing
 *   +0x30: socket identifier (2 bytes)
 *   +0x36: secondary socket (2 bytes)
 *   +0x38: port event count structure (0x24 bytes)
 */
/*
 * route_$driver_info_t - the per-port driver record route_$port_t.driver_info
 * (+0x48) points at.  Only two fields have been recovered so far.
 */
typedef struct route_$driver_info_t {
    uint16_t    _unknown0;      /* 0x00 */
    uint16_t    max_data_len;   /* 0x02: largest data length this port will
                                 *       carry.  PKT_$BLD_INTERNET_HDR compares
                                 *       the data length against it and then
                                 *       against it plus 0x100 together with
                                 *       the template ("cmp.w (0x2,A0),D3w" at
                                 *       0x00E1211E and "move.w (0x2,A0),D6w /
                                 *       addi.l #0x100,D6" at 0x00E12136);
                                 *       MSG_$$SEND repeats both tests at
                                 *       0x00E0DAD0 and 0x00E0DAEC. */
    uint16_t    _unknown4;      /* 0x04 */
    uint8_t     _unknown6;      /* 0x06 */
    uint8_t     flags;          /* 0x07: ROUTE_$VALIDATE_PORT reads this byte */
    uint8_t     _unknown8[0x0C];/* 0x08 */
    /*
     * The three driver entries ROUTE_$SERVICE calls while a port changes
     * status.  All three are held as target VAs (like set_service below), so
     * the record keeps its m68k size on a 64-bit host; reach them with
     * ARCH_VA_TO_PTR.  Each may be zero, and ROUTE_$SERVICE tests for that
     * before calling.
     */
    uint32_t    leave_status_1; /* 0x14: called when the port is leaving
                                 *       status 1 ("movea.l (0x14,A3),A0 /
                                 *       jsr (A0)" at 0x00E6A416), shape
                                 *       route_$port_status_fn_t */
    uint32_t    enter_status_1; /* 0x18: called once the port has reached
                                 *       status 1 ("movea.l (0x18,A3),A0 /
                                 *       jsr (A0)" at 0x00E6A53A), same shape */
    uint32_t    attach_service; /* 0x1C: called right after leave_status_1
                                 *       succeeds ("movea.l (0x1c,A3),A1 /
                                 *       jsr (A1)" at 0x00E6A43E) with the
                                 *       same five arguments and word result
                                 *       slot as set_service below, i.e.
                                 *       route_$set_service_fn_t */
    uint32_t    _unknown20;     /* 0x20 */
    uint32_t    set_service;    /* 0x24: driver entry point NETWORK_$SET_SERVICE
                                 *       calls after every successful update
                                 *       ("movea.l (0x24,A1),A0 / jsr (A0)" at
                                 *       0x00E0F5E4).  Held as a target VA, so
                                 *       reach it with ARCH_VA_TO_PTR. */
} route_$driver_info_t;

_Static_assert(offsetof(route_$driver_info_t, max_data_len) == 0x02,
               "route_$driver_info_t.max_data_len");
_Static_assert(offsetof(route_$driver_info_t, flags) == 0x07,
               "route_$driver_info_t.flags");
_Static_assert(offsetof(route_$driver_info_t, leave_status_1) == 0x14,
               "route_$driver_info_t.leave_status_1");
_Static_assert(offsetof(route_$driver_info_t, enter_status_1) == 0x18,
               "route_$driver_info_t.enter_status_1");
_Static_assert(offsetof(route_$driver_info_t, attach_service) == 0x1C,
               "route_$driver_info_t.attach_service");
_Static_assert(offsetof(route_$driver_info_t, set_service) == 0x24,
               "route_$driver_info_t.set_service");

/*
 * route_$set_service_fn_t - the driver entry at route_$driver_info_t+0x24
 *
 * NETWORK_$SET_SERVICE (0x00E0F5CA) pushes, right to left:
 *   subq.l #0x2,SP            ; word result slot; the result is discarded
 *   pea (-0x90,A6)            ; arg 5, an uninitialised local
 *   pea (-0x9a,A6)            ; arg 4, an uninitialised local
 *   move.w #0x88,-(SP)        ; arg 3, by value
 *   pea (-0x88,A6)            ; arg 2, the two-word record {0, service}
 *   pea (0xe2e0d0).l          ; arg 1, &ROUTE_$PORT_ARRAY[0].socket
 * NETWORK_$SET_SERVICE neither initialises nor reads arguments 4 and 5, but
 * the driver on the other end settles argument 5: RING_$IOCTL (0x00E76B2C)
 * is the entry it reaches through slot 0x24, and it takes that slot as its
 * status return - 0x00E76B42 `movea.l (0x16,A6),A3`, then
 * 0x00E76B4C `move.l #0x310002,(A3)`, 0x00E76B68 `clr.l (A3)` and
 * 0x00E76B6C `move.l #0x310001,(A3)`.  ROUTE_$SERVICE agrees: at
 * 0x00E6A42E `pea (A0)` it passes its own caller's status_$t * in that position.
 * Argument 4 is still unproven - RING_$IOCTL never reads (0x12,A6).
 * (source-hi9m)
 */
typedef int16_t (*route_$set_service_fn_t)(uint16_t *socket_ptr,
                                           const uint16_t *service_rec,
                                           uint16_t request,
                                           void *out4,
                                           status_$t *status_ret);

/*
 * route_$port_status_fn_t - the driver entries at route_$driver_info_t+0x14
 * and +0x18.  ROUTE_$SERVICE pushes exactly two longwords and pops them with
 * "addq.w #0x8,SP" (0x00E6A40E-0x00E6A41C, 0x00E6A532-0x00E6A540), so there
 * is no result slot:
 *   pea (0x30,A2)           ; arg 1, &port->socket
 *   move.l (0x10,A6),-(SP)  ; arg 2, the caller's status_$t *
 */
typedef void (*route_$port_status_fn_t)(uint16_t *socket_ptr,
                                        status_$t *status_ret);

typedef struct route_$port_t {
    uint32_t    network;            /* 0x00: Network address */
    /*
     * 0x04 and 0x06 are two words, and MAC_OS_$INIT sets BOTH to 1 with one
     * store - `move.l #0x10001,(0x4,A0)` at 0x00E2F590, A0 being the port
     * ROUTE_$PORTP[i] points at.
     *
     * n_net_addrs is proven: MAC_OS_$PUT_INFO bounds two nested loops with
     * it - `move.w (0x6,A0),D2w / subq.w #0x1,D2w / bmi` at 0x00E0C28E and
     * the same on the other port at 0x00E0C29C - and each loop walks
     * 12-byte records starting at port+0x20, stepping with
     * `lea (0xc,A2),A2` (0x00E0C2F4) and `add.l #0xc` (0x00E0C2FE).  Those
     * records are the xns_addr field below.
     *
     * n_link_addrs has no proven reader in this image.  It is named from
     * the record's shape: exactly one 24-byte mac_os_$link_addr_t sits at
     * +0x08, the way exactly one 12-byte rip_$dest_addr_t sits at +0x20,
     * and the two counts are initialised together.
     * TODO: find a reader of +0x04 (bead source-p046 follow-up).
     */
    uint16_t    n_link_addrs;       /* 0x04: 1 at init (0x00E2F590) */
    uint16_t    n_net_addrs;        /* 0x06: 1 at init; MAC_OS_$PUT_INFO's
                                     *       loop bound (0x00E0C28E) */
    /*
     * 0x08..0x1F: this port's own link-level address.  MAC_OS_$INIT builds
     * it as the two-word Apollo ring node id:
     *   0x00E2F59C  move.w #0x2,(0x8,A2)        n_words = 2
     *   0x00E2F5B4  move.w D1w,(0xa,A2)         addr[0] = (NODE_$ME >> 16) & 0xF
     *   0x00E2F5B8  move.w (0x00e245a6).l,(0xc,A2)  addr[1] = NODE_$ME low word
     * which is the same two-word form MAC_OS_$ARP writes for net_type 0 and
     * 3 (see mac_os_$link_addr_t).  The record is 0x18 bytes, so it ends
     * exactly where xns_addr begins.
     */
    mac_os_$link_addr_t link_addr;  /* 0x08..0x1F */
    rip_$dest_addr_t xns_addr;      /* 0x20: this port's own XNS endpoint
                                     *       {network, 6-byte host, socket}.
                                     *       RIP_$SEND_TO_PORT copies all 12
                                     *       bytes into the IDP header's source
                                     *       field ("lea (0x20,A3),A1" at
                                     *       0x00E87134) and then overwrites
                                     *       the socket half with 1. */
    uint16_t    active;             /* 0x2C: routing-capability BIT NUMBER;
                                     *       tested with "btst.l D0,D1" against
                                     *       0x28 (standard) and 0x30
                                     *       (non-standard) at 0x00E87240 /
                                     *       0x00E87258 */
    uint16_t    port_type;          /* 0x2E: Port type (1=local, 2=routing) */
    uint16_t    socket;             /* 0x30: Socket identifier */
    uint16_t    _unknown1a;         /* 0x32 */
    /*
     * 0x34 and 0x36 are read two ways and the boundary is not settled, so
     * they are spelled as words and each user says which it touches (the same
     * treatment as the 0x4C block below):
     *
     *   ROUTE_$SHORT_PORT copies the WORD at +0x36 into its fourth slot
     *   ("move.w (0x36,A0),(0xa,A1)" at 0x00E69C22).
     *
     *   ROUTE_$READ_USER_STATS reads the WORD at +0x36 as a signed bucket
     *   count ("move.w (0x36,A1),D4w / bmi" at 0x00E6A6C4, then "dbf" for
     *   count+1 longwords) AND the LONGWORD at +0x34 as the same count
     *   ("move.l (0x34,A1),D0 / addq.l #1 / lsl.l #2" at 0x00E6A6E4), so
     *   +0x36 is that longword's low half.
     *
     * Words keep both views expressible without a byte cast, so they behave
     * the same on a little-endian host.
     */
    uint16_t    queue_len_hi;       /* 0x34: high half of the +0x34 longword */
    uint16_t    socket2;            /* 0x36: ROUTE_$SHORT_PORT's fourth word;
                                     *       also that longword's low half */
    uint8_t     port_ec[0x0C];      /* 0x38: Port event count (ec_$eventcount_t, 12 bytes) */
    uint32_t    driver_stats;       /* 0x44: Driver statistics block pointer (32-bit
                                     *       address; ROUTE_$SEND_USER_PORT:
                                     *       movea.l (0x44,A0,D0),A2) */
    uint32_t    driver_info;        /* 0x48: route_$driver_info_t * as a 32-bit
                                     *       target address (same treatment as
                                     *       driver_stats above), reached with
                                     *       ARCH_VA_TO_PTR */
    /*
     * 0x4C..0x57.  Two writers and one reader disagree about the field
     * boundaries here, so the block is spelled as words and each user says
     * which ones it touches:
     *
     *   NET_IO_$CREATE_PORT (net_io/create_port.c) stores longwords through
     *   a byte pointer at +0x4C (the value 2, 0x00E5A582), +0x50 (the port's
     *   creation time, 0x00E5A58E) and +0x54 (zero, 0x00E5A586).
     *
     *   ASKNODE_$INTERNET_INFO's request-0x3D / request-0x5B arm reads a
     *   LONGWORD at +0x4E ("move.l (0x4e,A0),(0x8,A1)" at 0x00E65184) and a
     *   WORD at +0x52 ("move.w (0x52,A0),(0xc,A1)" at 0x00E6518A), i.e. it
     *   straddles the two longwords above.
     *
     * Words keep both views expressible without a byte cast, so the reads
     * behave the same on a little-endian host.
     * TODO: recover the real field boundaries (bead source-i54r).
     */
    uint16_t    _unknown2a;         /* 0x4C */
    uint16_t    _unknown2b;         /* 0x4E: high half of ASKNODE's +0x4E long */
    uint16_t    _unknown2c;         /* 0x50: low half of that long */
    uint16_t    _unknown2d;         /* 0x52: ASKNODE's +0x52 word */
    uint32_t    stat_long_54;       /* 0x54: cleared by NET_IO_$CREATE_PORT;
                                     *       first half of the 8-byte block
                                     *       ASKNODE copies to reply+0x0E */
    uint32_t    forward_count;      /* 0x58: Packets forwarded to this port (ROUTE_$PROCESS) */
} route_$port_t;

/* Port entry size must match the original 0x5C-byte stride */
_Static_assert(offsetof(route_$port_t, network)       == 0x00, "route_$port_t.network");
_Static_assert(offsetof(route_$port_t, n_link_addrs)  == 0x04, "route_$port_t.n_link_addrs");
_Static_assert(offsetof(route_$port_t, n_net_addrs)   == 0x06, "route_$port_t.n_net_addrs");
_Static_assert(offsetof(route_$port_t, link_addr)     == 0x08, "route_$port_t.link_addr");
_Static_assert(sizeof(((route_$port_t *)0)->link_addr) == 0x18,
               "route_$port_t.link_addr is one mac_os_$link_addr_t, 0x08..0x1F");
_Static_assert(offsetof(route_$port_t, xns_addr)      == 0x20, "route_$port_t.xns_addr");
_Static_assert(offsetof(route_$port_t, active)        == 0x2C, "route_$port_t.active");
_Static_assert(offsetof(route_$port_t, port_type)     == 0x2E, "route_$port_t.port_type");
_Static_assert(offsetof(route_$port_t, socket)        == 0x30, "route_$port_t.socket");
_Static_assert(offsetof(route_$port_t, queue_len_hi)  == 0x34, "route_$port_t.queue_len_hi");
_Static_assert(offsetof(route_$port_t, socket2)       == 0x36, "route_$port_t.socket2");
_Static_assert(offsetof(route_$port_t, socket2)       == 0x36, "route_$port_t.socket2");
_Static_assert(offsetof(route_$port_t, port_ec)       == 0x38, "route_$port_t.port_ec");
_Static_assert(offsetof(route_$port_t, driver_stats)  == 0x44, "route_$port_t.driver_stats");
_Static_assert(offsetof(route_$port_t, driver_info)   == 0x48, "route_$port_t.driver_info");
_Static_assert(offsetof(route_$port_t, _unknown2a)   == 0x4C, "route_$port_t._unknown2a");
_Static_assert(offsetof(route_$port_t, _unknown2b)   == 0x4E, "route_$port_t._unknown2b");
_Static_assert(offsetof(route_$port_t, _unknown2c)   == 0x50, "route_$port_t._unknown2c");
_Static_assert(offsetof(route_$port_t, _unknown2d)   == 0x52, "route_$port_t._unknown2d");
_Static_assert(offsetof(route_$port_t, stat_long_54) == 0x54, "route_$port_t.stat_long_54");
_Static_assert(offsetof(route_$port_t, forward_count) == 0x58, "route_$port_t.forward_count");
_Static_assert(sizeof(route_$port_t) == 0x5C, "route_$port_t must be 0x5C bytes");

/*
 * route_$port_stats_t - the statistics block a routing port points at
 *
 * route_$port_t.driver_stats (+0x44) holds the address of this block.
 * ROUTE_$PROCESS updates it after handing a forwarded packet to a user
 * routing port (0x00E87618 - 0x00E87664) and ROUTE_$READ_USER_STATS copies
 * the first ten bytes plus a port-dependent number of queue-depth buckets
 * out to the caller (0x00E6A6xx).
 *
 * The longwords sit on odd-numbered word boundaries (0x02, 0x06, 0x0A+n*4),
 * so the record has to be packed to lay out the same way off m68k.
 */
typedef struct route_$port_stats_t {
    uint16_t    flags;              /* 0x00: byte 0 is the record's "in use"
                                     *       boolean - NET_IO_$CREATE_PORT
                                     *       tests it with "tst.b"/"bmi"
                                     *       (0x00E5A5D0) and sets it with
                                     *       "st (A1)" (0x00E5A682),
                                     *       ROUTE_$CLOSE_PORT clears it with
                                     *       "clr.b (A0)" (0x00E69F9E), and
                                     *       ROUTE_$READ_USER_STATS copies it
                                     *       out (0x00E6A6B4).  Byte 1 has no
                                     *       accessor. */
    uint32_t    deep_queue_puts;    /* 0x02: SOCK_$PUT succeeded with a socket
                                     *       queue depth above 0x20
                                     *       (addq.l #1,(0x2,A2) at 0xE8764E) */
    uint32_t    failed_puts;        /* 0x06: SOCK_$PUT failed
                                     *       (addq.l #1,(0x6,A2) at 0xE87660) */
    uint32_t    queue_depth[0x21];  /* 0x0A: SOCK_$PUT succeeded, bucketed by
                                     *       the socket queue depth 0..0x20
                                     *       (addq.l #1,(0xA,A2,D1) at 0xE8765A) */
} __attribute__((packed)) route_$port_stats_t;

/*
 * The blocks these pointers refer to are the four records of ROUTE_$USER_STAT
 * (0xE87FD6, SAU2 map); the array below models it and explains why the record
 * stride there is 0x90 rather than this 0x8E.
 */

_Static_assert(offsetof(route_$port_stats_t, deep_queue_puts) == 0x02,
               "route_$port_stats_t.deep_queue_puts");
_Static_assert(offsetof(route_$port_stats_t, failed_puts) == 0x06,
               "route_$port_stats_t.failed_puts");
_Static_assert(offsetof(route_$port_stats_t, queue_depth) == 0x0A,
               "route_$port_stats_t.queue_depth");
_Static_assert(sizeof(route_$port_stats_t) == 0x8E,
               "route_$port_stats_t must be 0x8E bytes");

/*
 * =============================================================================
 * ROUTE_$USER_STAT - per-user-port statistics records (0xE87FD6 .. 0xE88216)
 * =============================================================================
 *
 * "User" ports are the EtherBridge ports /etc/rtsvc calls "-device USER";
 * NET_IO_$CREATE_PORT owns this array and hands one record to each such port.
 * Its allocator is a linear scan of four records of 0x90 bytes:
 *
 *   00e5a5c2  moveq   #0x3,D0        ; dbf count -> four records
 *   00e5a5c4  movea.l #0xe87fd6,A0   ; ROUTE_$USER_STAT
 *   00e5a5ca  moveq   #0x1,D1        ; record number, 1-based
 *   00e5a5cc  lea     (0x90,A0),A0   ; stride 0x90
 *   00e5a5d0  tst.b   (-0x90,A0)     ; record byte 0 = "in use" boolean
 *   00e5a5d4  bmi.b   0x00e5a5e0     ; true -> record taken, try the next
 *   ...
 *   00e5a5e2  lea     (0x90,A0),A0
 *   00e5a5e6  dbf     D0w,0x00e5a5d0
 *
 * The chosen record is then addressed as ROUTE_$USER_STAT + n*0x90 - 0x90
 * (n*0x90 is built as n<<4 + n<<7 at 0x00E5A658 - 0x00E5A65E, and the -0x90
 * bias is "lea (-0x90,A1),A1" at 0x00E5A664), stored in the port entry's
 * driver_stats field ("move.l A1,(0x44,A3)" at 0x00E5A668) and marked in use
 * with "st (A1)" at 0x00E5A682.  ROUTE_$CLOSE_PORT releases it with
 * "movea.l (0x44,A3),A0 / clr.b (A0)" at 0x00E69F9A.
 *
 * 4 * 0x90 == 0x240 == 0xE88216 - 0xE87FD6, i.e. exactly the span between the
 * SAU2 map's ROUTE_$USER_STAT and the next symbol, ROUTE_$PID.
 *
 * The body of a record is route_$port_stats_t (route/route.h): its byte 0 is
 * the in-use boolean above (the byte ROUTE_$READ_USER_STATS copies out at
 * 0x00E6A6B4 and ROUTE_$CLOSE_PORT clears), and the counters at 0x02, 0x06
 * and 0x0A are the ones ROUTE_$PROCESS bumps at 0x00E8764E, 0x00E87660 and
 * 0x00E8765A.  That record ends at 0x8D; nothing in the image reads or writes
 * 0x8E or 0x8F, so they are carried here as unnamed tail bytes of the 0x90
 * stride.
 *
 * ORIGINAL BUG (reproduced, not fixed - bead source-2km0): the record clear
 * loop at 0x00E5A66C - 0x00E5A67E is "move.w #0x90,D1w / clr.w D0w /
 * clr.b (0x0,A1,D0w) / addq.w #0x1,D0w / dbf D1w", i.e. 0x91 iterations
 * writing offsets 0x00..0x90.  It zeroes one byte past the end of the record;
 * for record 4 that byte is the first byte of ROUTE_$PID (0xE88216).
 */

#define ROUTE_$MAX_USER_STATS   4

typedef struct route_$user_stat_t {
    route_$port_stats_t stats;      /* 0x00: see route/route.h */
    uint8_t             _tail_8e[2];/* 0x8E: no accessor anywhere in the image;
                                     *       present only because the record
                                     *       stride is 0x90 (0x00E5A5CC,
                                     *       0x00E5A658) while every named
                                     *       field ends at 0x8D */
} __attribute__((packed)) route_$user_stat_t;

_Static_assert(sizeof(route_$user_stat_t) == 0x90,
               "route_$user_stat_t must be 0x90 bytes");
_Static_assert(sizeof(route_$user_stat_t) * ROUTE_$MAX_USER_STATS
                   == 0xE88216 - 0xE87FD6,
               "ROUTE_$USER_STAT must span 0xE87FD6..0xE88216");

/* Number of network ports supported */
#define ROUTE_$MAX_PORTS        8

/*
 * ROUTE_$PORT_ARRAY - Array of routing port structures
 *
 * Array of 8 port structures, each 0x5C (92) bytes.
 * Total size: 8 * 92 = 736 bytes (0x2E0)
 *
 * Original address: 0xE2E0A0
 */
extern route_$port_t ROUTE_$PORT_ARRAY[ROUTE_$MAX_PORTS];

/* Port type constants */
#define ROUTE_PORT_TYPE_LOCAL       1
#define ROUTE_PORT_TYPE_ROUTING     2

/*
 * ROUTE_$PORT - Current node's network port
 *
 * Contains the network port identifier for this node.
 * Set by HINT_$INIT from the hint file, or 0 if not available.
 *
 * This is the same storage as ROUTE_$PORT_ARRAY[0].network (the first
 * longword of the first port entry); see route_data.c.
 *
 * Original address: 0xE2E0A0
 */
extern uint32_t ROUTE_$PORT;

/*
 * Short port info structure (12 bytes)
 *
 * Compact representation of port information.  ROUTE_$SHORT_PORT fills one
 * from a route_$port_t and ROUTE_$SERVICE reads its request out of one, so
 * the two views must agree; ROUTE_$SERVICE's field reads pin the boundaries
 * that ROUTE_$SHORT_PORT's single "move.l (0x2c,A0),(0x4,A1)" (0x00E69C16)
 * leaves ambiguous:
 *
 *   +0x04 word   compared against port->active   (0x00E6A3A8, 0x00E6A44C)
 *   +0x06 word   compared against 1 and 2, i.e. a port type
 *                (0x00E6A072, 0x00E6A150, 0x00E6A19C)
 *   +0x08 word   passed to ROUTE_$FIND_PORT as the socket (0x00E6A1B6)
 *   +0x0A word   the user-port queue length (0x00E6A09A, 0x00E6A13E)
 *
 * so +0x04..+0x07 is not one "host id" longword but port->active followed by
 * port->port_type, exactly as they sit at port+0x2C/+0x2E.
 */
typedef struct route_$short_port_t {
    uint32_t    network;            /* 0x00: Network address (port+0x00) */
    uint16_t    status;             /* 0x04: Port status/active (port+0x2C) */
    uint16_t    port_type;          /* 0x06: Port type, 1=local 2=routing
                                     *       (port+0x2E) */
    uint16_t    socket;             /* 0x08: Socket identifier (port+0x30) */
    uint16_t    queue_length;       /* 0x0A: Secondary socket (port+0x36); the
                                     *       user-port queue length on the way
                                     *       in to ROUTE_$SERVICE */
} route_$short_port_t;

_Static_assert(offsetof(route_$short_port_t, status) == 0x04,
               "route_$short_port_t.status");
_Static_assert(offsetof(route_$short_port_t, port_type) == 0x06,
               "route_$short_port_t.port_type");
_Static_assert(offsetof(route_$short_port_t, socket) == 0x08,
               "route_$short_port_t.socket");
_Static_assert(offsetof(route_$short_port_t, queue_length) == 0x0A,
               "route_$short_port_t.queue_length");
_Static_assert(sizeof(route_$short_port_t) == 12,
               "route_$short_port_t must be 12 bytes");

/*
 * ROUTE_$FIND_PORT - Find port index by port type / socket
 *
 * Walks ROUTE_$PORTP[0..7] and returns the index of the first entry whose
 * active word is non-zero and whose PORT TYPE and socket both match.  The
 * first argument is a port type, not a network address: 0x00E15B1E compares
 * it against port+0x2E (route_$port_t.port_type) and 0x00E15B24 compares the
 * second against the sign-extended port+0x30 (route_$port_t.socket).
 *
 * @param port_type Port type to match, 1 = local, 2 = routing (port+0x2E)
 * @param socket    Socket identifier to match (sign-extended to 32-bit)
 *
 * @return Port index (0-7) if found, -1 if not found
 *
 * Original address: 0x00E15AF8
 */
int16_t ROUTE_$FIND_PORT(uint16_t port_type, int32_t socket);

/*
 * ROUTE_$FIND_PORTP - Find port structure by network/socket
 *
 * Similar to ROUTE_$FIND_PORT, but returns a pointer to the port
 * structure instead of the port index. Useful when direct access
 * to the port structure is needed.
 *
 * @param network   Network identifier to match
 * @param socket    Socket identifier to match (sign-extended to 32-bit)
 *
 * @return Pointer to port structure if found, NULL if not found
 *
 * Original address: 0x00E15B46
 */
route_$port_t *ROUTE_$FIND_PORTP(uint16_t network, int32_t socket);

/*
 * ROUTE_$SHORT_PORT - Extract short port info from port structure
 *
 * Copies key fields from a full port structure into a compact 12-byte
 * format suitable for passing to other functions.
 *
 * Output format (12 bytes, one route_$short_port_t):
 *   +0x00: network (4 bytes) - from port_struct+0x00
 *   +0x04: status + port_type (4 bytes) - one move.l from port_struct+0x2C
 *   +0x08: socket (2 bytes) - from port_struct+0x30
 *   +0x0A: queue_length/socket2 (2 bytes) - from port_struct+0x36
 *
 * @param port_struct   Source port structure pointer
 * @param short_info    Output: 12-byte compact port info
 *
 * Original address: 0x00E69C08
 */
void ROUTE_$SHORT_PORT(route_$port_t *port_struct, route_$short_port_t *short_info);

/*
 * ROUTE_$GET_EC - Get event count for a port
 *
 * Registers and returns an event count for the specified port.
 * The port is identified by network/socket pair within the port_info
 * structure. Supports two EC types: socket EC (type 0) and port EC (type 1).
 *
 * @param port_info   Port information structure with network at +6, socket at +8
 * @param ec_type     Pointer to EC type: 0 = socket EC, 1 = port EC
 * @param ec_ret      Output: pointer to registered event count
 * @param status_ret  Output: status code
 *
 * Status codes:
 *   status_$ok: Success
 *   status_$internet_unknown_network_port (0x2B0003): Port not found
 *   status_$route_illegal_op_for_port_type (0x2B0009): Port not in routing mode
 *   status_$route_invalid_ec_type (0x2B0012): Invalid EC type
 *
 * Original address: 0x00E69C2C
 */
void ROUTE_$GET_EC(void *port_info, int16_t *ec_type, void **ec_ret,
                   status_$t *status_ret);

/*
 * ROUTE_$SERVICE - Main routing service entry point
 *
 * Handles routing service requests for a specific port. This is the
 * central function for managing route operations.
 *
 * @param operation     Pointer to the 16-bit operation SET.  Every test is
 *                      "btst.b #n,(0x1,A3)" (0x00E6A056 onwards), i.e. bit n
 *                      of the word's low byte; callers pass the address of a
 *                      constant word (ROUTE_$SHUTDOWN's 0x0008 at 0x00E6A65A
 *                      and 0x0002 at 0x00E6A65C).
 * @param port_info     12-byte request/reply record; ROUTE_$SERVICE reads the
 *                      request out of it and overwrites it with
 *                      ROUTE_$SHORT_PORT's answer on the way out.
 * @param status_ret    Output: status code
 *
 * Original address: 0x00E6A030
 */
void ROUTE_$SERVICE(const uint16_t *operation, route_$short_port_t *port_info,
                    status_$t *status_ret);

/*
 * ROUTE_$SHUTDOWN - Shutdown all routing ports
 *
 * Iterates through all active routing ports and calls ROUTE_$SERVICE
 * to shut them down gracefully. Uses different shutdown codes based
 * on port type:
 *   - Port type 1 (local) or 2 (routing): uses operation code at 0xe6a65a
 *   - Other types: uses operation code at 0xe6a65c with
 *                  shutdown type 2 (first port) or 1 (subsequent)
 *
 * Original address: 0x00E6A5DC
 */
void ROUTE_$SHUTDOWN(void);

/*
 * ROUTE_$READ_USER_STATS - Read user-visible routing statistics
 *
 * Retrieves routing statistics for a user-mode port.
 * Looks up the port by socket number (assuming network type 2),
 * then copies statistics data from the port's driver structure.
 *
 * FIVE argument slots, read off the prologue (frame is "link.w A6,-0x14", so
 * arguments start at A6+0x08):
 *
 *   +0x08 socket_ptr   longword  movea.l (0x8,A6),A0   0x00E6A670
 *   +0x0C stats_buf    longword  movea.l (0xc,A6),A2   0x00E6A6AA
 *   +0x10 reserved     WORD      never referenced
 *   +0x12 length_ret   longword  move.l (0x12,A6),D2   0x00E6A666
 *   +0x16 status_ret   longword  move.l (0x16,A6),D3   0x00E6A66A
 *
 * The gap is a real argument, not padding: NET_IO_$DEVICE_STAT pushes five
 * arguments plus a discarded word result slot into this driver slot
 * (0x00E5A3FE - 0x00E5A414), the third being "move.w (0xc,A6),-(SP)" - a word
 * forwarded from its own caller.  Neither driver that fills the slot reads
 * it: ROUTE_$READ_USER_STATS ignores A6+0x10 and so does RING_$GET_STATS
 * (0x00E76950), which has the identical frame.  Its meaning is unrecovered.
 *
 * The call site also reserves a word function result that this routine never
 * writes - its epilogue is a plain rts.
 *
 * @param socket_ptr    Pointer to socket number (uint16_t)
 * @param stats_buf     Output buffer for statistics data
 * @param reserved      Word argument the dispatcher forwards but no driver
 *                      reads
 * @param length_ret    Output: number of bytes written to stats_buf
 * @param status_ret    Output: status code (status_$ok or error)
 *
 * Original address: 0x00E6A65E
 */
void ROUTE_$READ_USER_STATS(uint16_t *socket_ptr, uint8_t *stats_buf,
                            uint16_t reserved, int16_t *length_ret,
                            status_$t *status_ret);

/*
 * ROUTE_$PROCESS - Process routing updates
 *
 * Main processing function for handling routing protocol updates.
 *
 * Original address: 0x00E873EC
 */
void ROUTE_$PROCESS(void);

/*
 * ROUTE_$INCOMING - Handle incoming routed packets
 *
 * Processes packets received from user routing ports that need
 * to be injected into the local network. Validates packet format,
 * copies data to network buffers, and queues for transmission.
 *
 * @param port_info     Port information structure (network at +6, socket at +8)
 * @param packet_data   Packet data buffer
 * @param length_ptr    Pointer to packet length
 * @param status_ret    Output: status code
 *
 * Original address: 0x00E878A8
 */
void ROUTE_$INCOMING(void *port_info, uint8_t *packet_data,
                     uint16_t *length_ptr, status_$t *status_ret);

/*
 * ROUTE_$OUTGOING - Handle outgoing routed packets
 *
 * Retrieves queued outgoing packets from user routing ports and
 * prepares them for transmission. Finds the routing next hop,
 * copies packet data, and optionally computes a checksum.
 *
 * @param port_info     Port information (network at +6, socket at +8)
 * @param nexthop_ret   Output: next hop network address (20-bit) + flag
 * @param packet_buf    Output: packet data buffer (4-byte header + data)
 * @param length_ret    Output: total packet length including header
 * @param status_ret    Output: status code
 *
 * Original address: 0x00E87A4E
 */
void ROUTE_$OUTGOING(void *port_info, uint32_t *nexthop_ret, uint8_t *packet_buf,
                     int16_t *length_ret, status_$t *status_ret);

/*
 * ROUTE_$SEND_USER_PORT - Send packet to user routing port
 *
 * Sends a packet through a user routing port for delivery. The packet
 * is copied to network buffers and queued to the socket.
 *
 * Parameter offsets are read off the prologue at 0x00E87C34; see
 * route/send_user_port.c for the full mapping.
 *
 * @param socket_ptr    Pointer to the port's socket number (A6+0x08)
 * @param src_addr      Source address info (A6+0x0c, never read)
 * @param hdr_va        Header source VA (A6+0x10)
 * @param hdr_len       Header length (A6+0x14)
 * @param src_pages     Source payload page array (A6+0x16)
 * @param src_data_va   Source payload VA, or 0 (A6+0x1a)
 * @param data_len      Payload byte count (A6+0x1e)
 * @param extra_ptr     Extra protocol info (A6+0x20, never read)
 * @param seq_ret       Output: packet sequence number (A6+0x24)
 * @param status_ret    Output: status code (A6+0x28)
 *
 * Original address: 0x00E87C34
 */
void ROUTE_$SEND_USER_PORT(uint16_t *socket_ptr, uint32_t src_addr, uint32_t hdr_va,
                           uint16_t hdr_len, uint32_t *src_pages,
                           uint32_t src_data_va, uint16_t data_len,
                           void *extra_ptr, uint16_t *seq_ret,
                           status_$t *status_ret);

/*
 * ROUTE_$VALIDATE_PORT - Check network capability for node
 *
 * Checks if a network operation is supported for the given routing info.
 * Iterates through ROUTE_$PORTP array looking for matching port.
 *
 * @param routing_key   Routing information
 * @param is_local      Non-zero if querying local node
 *
 * @return 0: Unknown network
 *         1: Network supports operation
 *         2: Operation not defined on hardware
 *
 * Original address: 0x00E65904
 */
int16_t ROUTE_$VALIDATE_PORT(int32_t routing_key, int8_t is_local);

/*
 * Status codes (module 0x2B = INTERNET / ROUTE)
 *
 * These are the single definitions of the status_$internet_* codes; other
 * subsystems (ring, rip, xns, ...) include this header rather than
 * redefining them.
 */
/* "network port not open" (SR10.4 stcodes 2b0001).  The single name for this
 * code: ROUTE_$INCOMING returns it for a port that is not in routing mode
 * (0x00E878A8), ROUTE_$OUTGOING for a port not in user/routing mode
 * (0x00E87C08) and ROUTE_$SERVICE when the port has no IDP channel
 * (0x00E88C1E); all three are the same "port not open" condition. */
#define status_$internet_network_port_not_open  0x2B0001
#define status_$internet_unknown_network_port   0x2B0003
#define status_$internet_illegal_port_type      0x2B0004
/* "operation not legal on this port type" (SR10.4 stcodes 2b0009).
 * The single name for this code: ROUTE_$GET_EC returns it for a port that is
 * not in routing mode (0x00E69C72) and NET_IO_$CREATE_PORT for a port type it
 * cannot create (0x00E5A4F6); both are the same "not legal for this port
 * type" condition. */
#define status_$route_illegal_op_for_port_type  0x2B0009
/* "routing not allowed at port with 0 network ID" (stcodes 2b0011).
 * ROUTE_$SERVICE returns it when the effective network is zero and the
 * effective status needs one ("move.l #0x2b0011,(A0)" at 0x00E6A262). */
#define status_$route_no_routing_zero_network   0x2B0011
#define status_$route_invalid_ec_type           0x2B0012
/* "routing service type not recognized" (stcodes 2b0006).  ROUTE_$SERVICE
 * raises it for a status word outside 1..5 ("move.l #0x2b0006,(A0)" at
 * 0x00E6A212). */
#define status_$route_service_type_bad          0x2B0006
/*
 * 0x2B0013 and 0x2B0014 are past the end of the "OS / internet routing"
 * module in both the SR10.2 and the SR10.4 status databases (which stop at
 * 0x2B0011), so their message text is unrecovered.  Both are raised only by
 * ROUTE_$SERVICE's user-port argument check: 0x2B0013 when the create bit is
 * missing ("move.l #0x2b0013,(A0)" at 0x00E6A092) and 0x2B0014 when the
 * requested queue length exceeds 0x20 ("move.l #0x2b0014,(A0)" at
 * 0x00E6A0A6).  The names below are descriptive, not from the database.
 */
#define status_$route_create_flag_required      0x2B0013
#define status_$route_queue_length_too_large    0x2B0014


/*
 * =============================================================================
 * ROUTE module data blocks (docs/design-per-process-data.md)
 * =============================================================================
 *
 * The ROUTE module owns three data segments in the SAU2 map; each is one
 * MODULE_DATA block (route/route_data.c) linked in the map's order.  The
 * address given with each is the block's image address - the ordering key of
 * tools/gen_layout_ld.py and documentation, not where the block is linked.
 * Every access, inside ROUTE and out, goes through the block.
 *
 * Module data blocks: Claude Opus 5.5 (source-ybch).
 */

/*
 * -----------------------------------------------------------------------------
 * ROUTE_$WIRED_DATA - the ROUTE_WIRED segment, 0x00E26EE4..0x00E26F1F
 * -----------------------------------------------------------------------------
 *
 * SAU2 map: "D E26EE4 ROUTE_WIRED size = 3C", after RIP_WIRED's
 * RIP_$RECENT_CHANGES (0xE26EE0) and before SMD_WIRED (0xE26F20, the code of
 * SMD_$DISP1_INT).  Every interior symbol is a field: ROUTE_$SOCK_ECVAL
 * (+0x00), ROUTE_$PORTP (+0x04), ROUTE_$CONTROL_ECVAL (+0x24),
 * ROUTE_$CONTROL_EC (+0x28), ROUTE_$SOCK (+0x34),
 * ROUTE_$STD_N_ROUTING_PORTS (+0x36), ROUTE_$N_ROUTING_PORTS (+0x38) and
 * ROUTE_$ROUTING (+0x3A).  ROUTE_$FIND_PORT and ROUTE_$FIND_PORTP load it as
 * A5 ("lea (0xe26ee4).l,A5" at 0x00E15B00 / 0x00E15B4E) and walk portp from
 * "movea.l (0x4,A0),A1" with A0 = A5 + i*4 (0x00E15B14), i = 0..7; everyone
 * else addresses the cells absolutely.
 *
 * portp holds pointers and control_ec is an ec_$eventcount_t (pointers), so
 * the offsets past sock_ecval and the size are asserted on the target only.
 * Image contents (`gsk read 0xE26EE4 0x3C`): portp[i] = 0xE2E0A0 + i*0x5C,
 * i.e. &ROUTE_$PORT_ARRAY[i]; sock = 0xFFFF; every other byte zero
 * (route/route_data.c).
 */
#define ROUTE_$WIRED_DATA_SIZE  0x3C    /* map: ROUTE_WIRED size = 3C */

typedef struct route_$wired_data_t {
    /* +0x00 map ROUTE_$SOCK_ECVAL: the routing socket's awaited eventcount
     * value (ROUTE_$PROCESS 0x00E87466, 0x00E877B8; route_$init_routing
     * 0x00E69DDE) */
    uint32_t          sock_ecval;
    /* +0x04 map ROUTE_$PORTP: the eight port pointers, 0-based [0..7] */
    route_$port_t    *portp[ROUTE_$MAX_PORTS];
    /* +0x24 map ROUTE_$CONTROL_ECVAL: awaited value of control_ec */
    uint32_t          control_ecval;
    /* +0x28 map ROUTE_$CONTROL_EC: the routing process's control eventcount
     * (EC_$INIT at 0x00E69D22, EC_$ADVANCE at 0x00E69E26) */
    ec_$eventcount_t  control_ec;
    /* +0x34 map ROUTE_$SOCK: the routing process's socket, 0xFFFF when
     * closed; XNS_IDP_$DEMUX queues packets to be forwarded on it */
    uint16_t          sock;
    /* +0x36 / +0x38 map ROUTE_$STD_N_ROUTING_PORTS / ROUTE_$N_ROUTING_PORTS:
     * routing-port counts, shared with RIP */
    int16_t           std_n_routing_ports;
    int16_t           n_routing_ports;
    /*
     * +0x3A map ROUTE_$ROUTING: "the router is running", a one-BYTE Domain
     * boolean.  ROUTE_$PROCESS sets it with "st (0x00E26F1E).l"
     * (0x00E8742E) and clears it with "clr.b (0x00E26F1E).l" (0x00E87812);
     * ROUTE_$CLEANUP_WIRED and the SMD readers test it with "tst.b"
     * (0x00E69B8C, 0x00E69E94).  SMD_$DISP1_INT is the code at 0x00E26F20,
     * right after the segment (bead source-8xb).
     */
    boolean           routing;
    uint8_t           _unknown_3b;  /* +0x3B: not referenced */
} route_$wired_data_t;

_Static_assert(offsetof(route_$wired_data_t, sock_ecval) == 0x00, "ROUTE_$SOCK_ECVAL");
#if defined(ARCH_M68K)
/* Pointer-bearing (portp, control_ec): target-only (design section 3). */
_Static_assert(offsetof(route_$wired_data_t, portp) == 0x04, "ROUTE_$PORTP (0xE26EE8)");
_Static_assert(sizeof(((route_$wired_data_t *)0)->portp[0]) == 4, "portp stride (addq.l #0x4,A0)");
_Static_assert(offsetof(route_$wired_data_t, control_ecval) == 0x24, "ROUTE_$CONTROL_ECVAL (0xE26F08)");
_Static_assert(offsetof(route_$wired_data_t, control_ec) == 0x28, "ROUTE_$CONTROL_EC (0xE26F0C)");
_Static_assert(sizeof(ec_$eventcount_t) == 0x0C, "ec_$eventcount_t size");
_Static_assert(offsetof(route_$wired_data_t, sock) == 0x34, "ROUTE_$SOCK (0xE26F18)");
_Static_assert(offsetof(route_$wired_data_t, std_n_routing_ports) == 0x36,
               "ROUTE_$STD_N_ROUTING_PORTS (0xE26F1A)");
_Static_assert(offsetof(route_$wired_data_t, n_routing_ports) == 0x38,
               "ROUTE_$N_ROUTING_PORTS (0xE26F1C)");
_Static_assert(offsetof(route_$wired_data_t, routing) == 0x3A, "ROUTE_$ROUTING (0xE26F1E)");
_Static_assert(sizeof(route_$wired_data_t) == ROUTE_$WIRED_DATA_SIZE, "ROUTE_WIRED: map size 0x3C");
#endif

MODULE_DATA_DECLARE(route_$wired_data_t, ROUTE_$WIRED_DATA, 0x00E26EE4);

/*
 * -----------------------------------------------------------------------------
 * ROUTE_$UNWIRED_DATA - the ROUTE_UNWIRED segment, 0x00E825DC..0x00E825E3
 * -----------------------------------------------------------------------------
 *
 * SAU2 map: "D E825DC ROUTE_UNWIRED size = 8", interior symbol
 * ROUTE_$START_TIME (+0x00); after TPAD (0xE8245C) and before VFMT_$FORMATN
 * (0xE825E4).  ROUTE_$SERVICE loads it as A5 ("lea (0xe825dc).l,A5" at
 * 0x00E6A038) and its nested route_$init_routing and ROUTE_$ANNOUNCE_NET
 * inherit it.  Pointer-free, so every assert is unconditional.  Image
 * contents (`gsk read 0xE825DC 8`): 00 00 00 00 00 02 00 00.
 */
#define ROUTE_$UNWIRED_DATA_SIZE 0x8    /* map: ROUTE_UNWIRED size = 8 */

typedef struct route_$unwired_data_t {
    /*
     * +0x00 map ROUTE_$START_TIME: TIME_$CURRENT_CLOCKH when routing started.
     * route_$init_routing stores it with "move.l (0x00e2b0e4).l,(A5)"
     * (0x00E69DEA), ROUTE_$PROCESS clears it on shutdown ("clr.l
     * (0x00E825DC).l" at 0x00E87818) and ASKNODE_$INTERNET_INFO's request-0x3F
     * arm reports it ("move.l (0x00E825DC).l,(0xc,A1)" at 0x00E650EE).  (The
     * tree also called it ROUTE_$LAST_UPDATE_TIME; the map name wins, bead
     * source-wm2s.)
     */
    uint32_t start_time;
    /*
     * +0x04: the two-byte RIP template ROUTE_$ANNOUNCE_NET sends, the word 2
     * (a RIP response with no entries), pushed as "pea (0x4,A5)" at
     * 0x00E69FF2 with A5 left at this block by ROUTE_$SERVICE.
     */
    uint16_t announce_template;
    uint16_t _unknown_06;       /* +0x06: not referenced */
} route_$unwired_data_t;

_Static_assert(offsetof(route_$unwired_data_t, start_time) == 0x00, "ROUTE_$START_TIME");
_Static_assert(offsetof(route_$unwired_data_t, announce_template) == 0x04,
               "announce template (pea (0x4,A5))");
_Static_assert(sizeof(route_$unwired_data_t) == ROUTE_$UNWIRED_DATA_SIZE, "ROUTE_UNWIRED: map size 8");

MODULE_DATA_DECLARE(route_$unwired_data_t, ROUTE_$UNWIRED_DATA, 0x00E825DC);

/*
 * -----------------------------------------------------------------------------
 * ROUTE_$RTWIRED_DATA - the ROUTE_RTWIRED segment, 0x00E87D80..0x00E88227
 * -----------------------------------------------------------------------------
 *
 * SAU2 map: "D E87D80 ROUTE_RTWIRED size = 4A8", after RIP_RTWIRED
 * (0xE87D68, rip's) and ending at RTWIRED_DATA_END (0xE88228), the end of
 * the wired routing region.  ROUTE_$PROCESS loads it as A5
 * ("lea (0xe87d80).l,A5" at 0x00E873F4); the other routines address its
 * cells absolutely.  Map-named fields: ROUTE_$WIRED_PAGES (+0x000),
 * ROUTE_$Q_DEPTH (+0x028), the nine forwarding counters ROUTE_$STD_DLEN_ERR
 * .. ROUTE_$Q_OFLO (+0x22C..+0x24C), ROUTE_$NETBUF_ALLOC (+0x250),
 * ROUTE_$N_WIRED_PAGES (+0x252), ROUTE_$N_USER_PORTS (+0x254),
 * ROUTE_$USER_STAT (+0x256), ROUTE_$PID (+0x496) and ROUTE_$USER_CHECKSUM
 * (+0x498); the last four cells carry tree names.
 *
 * Per-index tables:
 *   wired_pages  0-based [0..9]: MST_$WIRE_AREA fills from the base
 *                (route_$wire_routing_area) and ROUTE_$CLEANUP_WIRED walks
 *                "lea (0x4,A0),A2 / move.l (-0x4,A2),-(SP)" (0x00E69BA6).
 *   q_depth      0-based [0..0x80], bucketed by queue depth:
 *                "addq.l #0x1,(0x28,A5,D1*0x1)" with D1 = depth*4 capped at
 *                0x80 (0x00E874E6); cleared as 0x81 longwords by
 *                route_$init_routing (0x00E69D12).
 *   user_stat    Pascal [1..4], record n at +0x256 + (n-1)*0x90:
 *                NET_IO_$CREATE_PORT scans from "movea.l #0xe87fd6,A0"
 *                (0x00E5A5C4) and forms base + n*0x90 then "lea (-0x90,A1),A1"
 *                (0x00E5A664); the bias slot is never addressed, so the table
 *                is declared from record 1 and ROUTE_USER_STAT_ENTRY(n)
 *                applies the bias once (as PKT_MISSING_ENTRY does).
 *
 * ptr_control_ec holds a pointer, so its successors and the size are asserted
 * on the target only.  Image contents (`gsk read 0xE87D80 1192`): all zero
 * except ptr_control_ec = 0x00E26F0C (&ROUTE_$WIRED_DATA.control_ec),
 * fwd_timeout = 1 and packet_seq = 0x8000 (route/route_data.c).
 */
#define ROUTE_$RTWIRED_DATA_SIZE 0x4A8  /* map: ROUTE_RTWIRED size = 4A8 */

/* Maximum number of pages to wire for routing (constant at 0xE69BFC) */
#define ROUTE_$MAX_WIRED_PAGES  10

/* ROUTE_$Q_DEPTH buckets: queue depths 0..0x80 */
#define ROUTE_$Q_DEPTH_BUCKETS  0x81

typedef struct route_$rtwired_data_t {
    /* +0x000 map ROUTE_$WIRED_PAGES: wired page handles */
    uint32_t wired_pages[ROUTE_$MAX_WIRED_PAGES];
    /*
     * +0x028 map ROUTE_$Q_DEPTH: forwarded packets bucketed by the routing
     * socket's queue depth.  ASKNODE_$INTERNET_INFO copies netbuf_alloc + 1
     * buckets into its reply (dbf at 0x00E6534E).
     */
    uint32_t q_depth[ROUTE_$Q_DEPTH_BUCKETS];
    /*
     * +0x22C..+0x248: forwarding counters, cleared one by one by
     * route_$init_routing (0x00E69DF0-0x00E69E20).  XNS_IDP_$OS_DEMUX bumps
     * two of them directly ("addq.l #0x1,(0x00E87FB4).l" at 0x00E18678,
     * "addq.l #0x1,(0x00E87FB0).l" at 0x00E1869A).  Earlier tree spellings
     * follow each map name.
     */
    uint32_t std_dlen_err;      /* +0x22C map ROUTE_$STD_DLEN_ERR (was ..._STAT_OVERSIZED_STD) */
    uint32_t std_too_far;       /* +0x230 map ROUTE_$STD_TOO_FAR (was ..._STAT_DROPPED_STD_HOP) */
    uint32_t std_misroute;      /* +0x234 map ROUTE_$STD_MISROUTE (was ..._STAT_DROPPED_STD_ROUTE) */
    uint32_t std_pkts_routed;   /* +0x238 map ROUTE_$STD_PKTS_ROUTED (was ..._STAT_FORWARDED_STD) */
    uint32_t dlen_err;          /* +0x23C map ROUTE_$DLEN_ERR (was ..._STAT_OVERSIZED_N) */
    uint32_t too_far;           /* +0x240 map ROUTE_$TOO_FAR (was ..._STAT_DROPPED_N_HOP) */
    uint32_t misroute;          /* +0x244 map ROUTE_$MISROUTE (was ..._STAT_DROPPED_N_ROUTE) */
    uint32_t pkts_routed;       /* +0x248 map ROUTE_$PKTS_ROUTED (was ..._STAT_FORWARDED_N) */
    /*
     * +0x24C map ROUTE_$Q_OFLO: packets addressed to the routing socket that
     * were dropped because its queue was full ("queue oflo" in /etc/rtstat).
     * Bumped by ring_$process_rx_packet (0x00E756C6) and FUN_00E0E238
     * (0x00E0E45E) when the full socket is ROUTE_$SOCK; reported by
     * ASKNODE_$INTERNET_INFO (0x00E65106, 0x00E65330).  (Was
     * ..._USER_PORT_COUNT.)
     */
    uint32_t q_oflo;
    /*
     * +0x250 map ROUTE_$NETBUF_ALLOC: the netbuf page count the routing socket
     * asks SOCK_$ALLOCATE for (0x40, "move.w #0x40,(0x00E87FD0).l" at
     * 0x00E69D80), zeroed by ROUTE_$PROCESS on shutdown ("clr.w (0x250,A5)"
     * at 0x00E87852); ASKNODE_$INTERNET_INFO's loop bound over q_depth.
     * (Was ..._USER_PORT_MAX.)
     */
    uint16_t netbuf_alloc;
    /*
     * +0x252 map ROUTE_$N_WIRED_PAGES: a word.  route_$wire_routing_area
     * tests it ("tst.w", 0x00E69B94) and passes its address to MST_$WIRE_AREA
     * (0x00E69BD2); ROUTE_$PROCESS reads it with "move.w (0x252,A5),D0w"
     * (0x00E8785C) and clears it (0x00E8787E).
     */
    int16_t  n_wired_pages;
    /*
     * +0x254 map ROUTE_$N_USER_PORTS: active user (EtherBridge) ports.
     * ROUTE_$SERVICE increments it ("addq.w #0x1,(0x00E87FD4).l" at
     * 0x00E6A1A4), route_$close_port decrements it (0x00E69F90), and
     * ROUTE_$CLEANUP_WIRED (0x00E69B84) and ROUTE_$PROCESS
     * ("tst.w (0x254,A5)" at 0x00E87856) test it.
     */
    int16_t  n_user_ports;
    /* +0x256 map ROUTE_$USER_STAT: records 1..4, see above */
    route_$user_stat_t user_stat[ROUTE_$MAX_USER_STATS];
    /* +0x496 map ROUTE_$PID: the routing process ("move.w (0x496,A5),-(SP)"
     * at 0x00E87888) */
    uint16_t pid;
    /* +0x498 map ROUTE_$USER_CHECKSUM: Domain boolean ROUTE_$OUTGOING tests
     * (0x00E87BE8) */
    int8_t   user_checksum;
    uint8_t  _unknown_499;      /* +0x499: not referenced */
    uint16_t _unknown_49a;      /* +0x49A: not referenced */
    /* +0x49C: the routing process's service id ("pea (0x49c,A5)" at
     * 0x00E87438 / 0x00E87822) */
    uint32_t service_id;
    /* +0x4A0: EC_$WAITN's one-element eventcount list ("pea (0x4a0,A5)" at
     * 0x00E87406); image value 0x00E26F0C, &ROUTE_$WIRED_DATA.control_ec */
    ec_$eventcount_t *ptr_control_ec;
    /* +0x4A4: send flags/timeout for the forwarded packet
     * ("move.w (0x4a4,A5),-(SP)" at 0x00E87732); 1 in the image */
    uint16_t fwd_timeout;
    /* +0x4A6: ROUTE_$SEND_USER_PORT's sequence counter (0x00E87D02);
     * 0x8000 in the image */
    uint16_t packet_seq;
} route_$rtwired_data_t;

_Static_assert(offsetof(route_$rtwired_data_t, wired_pages) == 0x000, "ROUTE_$WIRED_PAGES");
_Static_assert(offsetof(route_$rtwired_data_t, q_depth) == 0x028, "ROUTE_$Q_DEPTH (0x28,A5)");
_Static_assert(sizeof(((route_$rtwired_data_t *)0)->q_depth[0]) == 4, "q_depth stride");
_Static_assert(offsetof(route_$rtwired_data_t, q_depth[ROUTE_$Q_DEPTH_BUCKETS]) == 0x22C,
               "q_depth[0x80] ends at ROUTE_$STD_DLEN_ERR");
_Static_assert(offsetof(route_$rtwired_data_t, std_dlen_err) == 0x22C, "ROUTE_$STD_DLEN_ERR (0xE87FAC)");
_Static_assert(offsetof(route_$rtwired_data_t, std_too_far) == 0x230, "ROUTE_$STD_TOO_FAR (0xE87FB0)");
_Static_assert(offsetof(route_$rtwired_data_t, std_misroute) == 0x234, "ROUTE_$STD_MISROUTE (0xE87FB4)");
_Static_assert(offsetof(route_$rtwired_data_t, std_pkts_routed) == 0x238, "ROUTE_$STD_PKTS_ROUTED (0xE87FB8)");
_Static_assert(offsetof(route_$rtwired_data_t, dlen_err) == 0x23C, "ROUTE_$DLEN_ERR (0xE87FBC)");
_Static_assert(offsetof(route_$rtwired_data_t, too_far) == 0x240, "ROUTE_$TOO_FAR (0xE87FC0)");
_Static_assert(offsetof(route_$rtwired_data_t, misroute) == 0x244, "ROUTE_$MISROUTE (0xE87FC4)");
_Static_assert(offsetof(route_$rtwired_data_t, pkts_routed) == 0x248, "ROUTE_$PKTS_ROUTED (0xE87FC8)");
_Static_assert(offsetof(route_$rtwired_data_t, q_oflo) == 0x24C, "ROUTE_$Q_OFLO (0xE87FCC)");
_Static_assert(offsetof(route_$rtwired_data_t, netbuf_alloc) == 0x250, "ROUTE_$NETBUF_ALLOC (0x250,A5)");
_Static_assert(offsetof(route_$rtwired_data_t, n_wired_pages) == 0x252, "ROUTE_$N_WIRED_PAGES (0x252,A5)");
_Static_assert(offsetof(route_$rtwired_data_t, n_user_ports) == 0x254, "ROUTE_$N_USER_PORTS (0x254,A5)");
_Static_assert(offsetof(route_$rtwired_data_t, user_stat) == 0x256, "ROUTE_$USER_STAT (0xE87FD6)");
_Static_assert(sizeof(((route_$rtwired_data_t *)0)->user_stat[0]) == 0x90, "user_stat stride (lea (0x90,A0),A0)");
_Static_assert(offsetof(route_$rtwired_data_t, pid) == 0x496, "ROUTE_$PID (0x496,A5)");
_Static_assert(offsetof(route_$rtwired_data_t, user_checksum) == 0x498, "ROUTE_$USER_CHECKSUM (0xE88218)");
_Static_assert(offsetof(route_$rtwired_data_t, service_id) == 0x49C, "service_id (0x49c,A5)");
#if defined(ARCH_M68K)
/* Pointer-bearing cell and what follows it: target-only (design section 3). */
_Static_assert(offsetof(route_$rtwired_data_t, ptr_control_ec) == 0x4A0, "ptr_control_ec (0x4a0,A5)");
_Static_assert(offsetof(route_$rtwired_data_t, fwd_timeout) == 0x4A4, "fwd_timeout (0x4a4,A5)");
_Static_assert(offsetof(route_$rtwired_data_t, packet_seq) == 0x4A6, "packet_seq (0xE88226)");
_Static_assert(sizeof(route_$rtwired_data_t) == ROUTE_$RTWIRED_DATA_SIZE, "ROUTE_RTWIRED: map size 0x4A8");
#endif

MODULE_DATA_DECLARE(route_$rtwired_data_t, ROUTE_$RTWIRED_DATA, 0x00E87D80);

/*
 * ROUTE_USER_STAT_ENTRY(n) - record n (1..4) of the Pascal [1..4] user_stat
 * table, an lvalue of type route_$user_stat_t.  NET_IO_$CREATE_PORT forms
 * ROUTE_$USER_STAT + n*0x90 and then "lea (-0x90,A1),A1" (0x00E5A664), and
 * its free-record scan tests "(-0x90,A0)" from base + 0x90 (0x00E5A5D0):
 * the code never forms an address below record 1, so - like
 * PKT_MISSING_ENTRY - the table is declared from record 1 and the -1 is the
 * compiler's -0x90 bias, applied here once.
 */
#define ROUTE_USER_STAT_ENTRY(n)    (ROUTE_$RTWIRED_DATA.user_stat[(n) - 1])

/*
 * ROUTE_$DECREMENT_PORT - Decrement port counters during close
 *
 * Helper that calls RIP_$PORT_CLOSE and decrements the appropriate routing
 * port counter; may halt the router if this was the last active port.
 * RIP_$PORT (rip/misc.c) calls it directly (0x00E15818), so it is public.
 *
 * @param delete_flag      Delete notification flag
 * @param port_index       Port index being closed
 * @param port_type_flag   Port type flag (negative = STD)
 *
 * Original address: 0x00E69E40
 */
void ROUTE_$DECREMENT_PORT(int8_t delete_flag, int16_t port_index,
                           int8_t port_type_flag);

/* RIP_$HALT_PACKET, RIP_$HALT_PACKET_DATA and RIP_$SEND_DEST_ADDR carry the
 * RIP_$ prefix, so they are declared in rip/rip.h (included above) even though
 * the storage is defined in route/route_data.c (bead source-3uo). */

/* RTWIRED_$SEND_FLAGS (0xE87D74) and RTWIRED_$CALLBACK (0xE870D8) sit inside
 * the RIP_RTWIRED segments (SAU2 map: I 0xE87000 size 0x3EC, D 0xE87D68 size
 * 0x18), so they are declared in rip/rip.h (included above) even though the
 * storage is defined in route/route_data.c (bead source-3uo). */

#endif /* ROUTE_H */
