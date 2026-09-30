/*
 * NET_IO - Network I/O Module
 *
 * This module provides low-level network I/O operations.
 */

#ifndef NET_IO_H
#define NET_IO_H

#include "base/base.h"

/*
 * net_io_$send_info_t - the two-word record NET_IO_$SEND reports through its
 * ninth argument.  It is written on every path:
 *
 *   0x00E0E75C  clr.w (A0) / ori.w #-0x7ff8,(0x2,A0)
 *               loopback: port_net = 0, xmit_status |= 0x8008
 *   0x00E0E82E  move.w (0x2e,A2),(A4) / clr.w (0x2,A4)
 *               real send: port_net = the port's network number, then
 *               xmit_status is cleared and handed to the driver by address
 *               (0x00E0E870 "pea (0x2,A0)").
 *
 * PKT_$SEND_INTERNET reads both words to decide whether a failed send is
 * worth retrying (0x00E12770 / 0x00E12776).
 */
typedef struct net_io_$send_info_t {
    uint16_t    port_net;       /* 0x00 */
    uint16_t    xmit_status;    /* 0x02 */
} net_io_$send_info_t;

/*
 * NET_IO_$SEND - Send a network packet
 *
 * Sends a packet over the network to a specified destination.
 *
 * Ten arguments; the caller pops 0x20 bytes (0x00E12754 "lea (0x20,SP),SP").
 * The prologue offsets are 0x08, 0x0A, 0x0E, 0x12, 0x14, 0x18, 0x1C, 0x1E,
 * 0x20, 0x24.
 *
 * @param port        Port/interface to send on (0x00E0E6A0, word)
 * @param hdr_ptr     Address of the header VA; dereferenced twice
 *                    (0x00E0E6FA "movea.l D3,A1 / movea.l (A1),A4")
 * @param hdr_pa      Header physical address (0x00E0E88A)
 * @param hdr_len     Header length (0x00E0E71C, word)
 * @param data_va     Data virtual address (0x00E0E718)
 * @param data_pages  The four data-page addresses PKT_$COPY_TO_PA filled in
 *                    (0x00E0E6A8 "move.l (0x18,A6),D4")
 * @param data_len    Data length (0x00E0E712, word)
 * @param flags       Send flags (0x00E0E878, word)
 * @param send_info   Output: net_io_$send_info_t (0x00E0E6AC)
 * @param status_ret  Output: status code (0x00E0E6B0)
 *
 * Original address: 0x00E0E692
 */
void NET_IO_$SEND(int16_t port, uint32_t *hdr_ptr, uint32_t hdr_pa,
                  uint16_t hdr_len, uint32_t data_va, uint32_t *data_pages,
                  int16_t data_len, uint16_t flags,
                  net_io_$send_info_t *send_info, status_$t *status_ret);

/*
 * NET_IO_$PUT_IN_SOCK - Put packet in socket
 *
 * Original address: 0x00E0E4A0
 */
/*
 * Arguments 3 and 4 are the ADDRESSES of two 32-bit cells, not the buffers
 * themselves: 0x00E0E4BE / 0x00E0E4BA forward them as longwords and the
 * callee at 0x00E0E2A0 does "movea.l (A4),A2" to reach the header.  Both
 * callers push the address of a netbuf VA / physical-address cell.
 */
void NET_IO_$PUT_IN_SOCK(uint16_t net_type, uint16_t socket, uint32_t *hdr_va_p,
                         uint32_t *data_pa_p, uint16_t hdr_len, uint16_t data_len);

/*
 * NET_IO_$COPY_PACKET - Copy a packet to network buffers
 *
 * Copies packet data from user buffers to network buffers suitable
 * for transmission.
 *
 * Parameter offsets read off the prologue (link.w A6,-0x1c):
 *   A6+0x08  hdr_src_p     "movea.l (0x8,A6),A1 / move.l (A1),-(SP)" at
 *                          0x00E0E67C: the address of a cell holding the
 *                          header SOURCE VA
 *   A6+0x0c  hdr_len       word, "move.w (0xc,A6),D5w"
 *   A6+0x0e  src_data_va   longword; when non-zero the payload is read
 *                          linearly from it ("add.l (0xe,A6),D7" at
 *                          0x00E0E60E), when zero it is read page by page
 *   A6+0x12  src_pages     "movea.l (0x12,A6),A4 / addq.l #4,A4" - the
 *                          source payload page array
 *   A6+0x16  data_len      word
 *   A6+0x18  hdr_va_out    passed BY VALUE to NETBUF_$GET_HDR as its va_out
 *                          (0x00E0E660), so it is already a pointer
 *   A6+0x1c  data_pages_out  "clr.l (A1)" / NETBUF_$GET_DAT fills it
 *   A6+0x20  status_ret
 *
 * @param hdr_src_p       Address of the header source VA cell
 * @param hdr_len         Header length
 * @param src_data_va     Source payload VA, or 0 to use src_pages
 * @param src_pages       Source payload page array
 * @param data_len        Data length
 * @param hdr_va_out      Output: new header buffer VA
 * @param data_pages_out  Output: new payload page VAs
 * @param status_ret      Output status code
 *
 * Original address: 0x00E0E514
 */
void NET_IO_$COPY_PACKET(uint32_t *hdr_src_p, uint16_t hdr_len,
                         uint32_t src_data_va, uint32_t *src_pages,
                         uint16_t data_len, uint32_t *hdr_va_out,
                         uint32_t *data_pages_out, status_$t *status_ret);

/*
 * NET_IO_$BOOT_DEVICE - Record the network boot device
 *
 * Parameters (two words at 8(A6) and 0xA(A6)):
 *   boot_device - boot device code (2/3, 6 and 8 are network devices)
 *   boot_unit   - the boot controller/unit word, stored in
 *                 NET_IO_UNWIRED.boot_unit for a network device
 *
 * Returns: -1 (0xFF) if booting over the network (diskless), 0 otherwise.
 * OS_$INIT stores the result in NETWORK_$DISKLESS.
 *
 * Original address: 0x00E31C14 (net_io/boot_device.c)
 */
int8_t NET_IO_$BOOT_DEVICE(uint16_t boot_device, uint16_t boot_unit);

/*
 * NET_IO_$CREATE_PORT - Create a network I/O port
 *
 * Stack frame (0x00E5A4A4): port_type word at 8(A6), unit word at 0xA(A6),
 * driver long at 0xC(A6), queue_length word at 0x10(A6), status_ret long
 * at 0x12(A6); result returned in D0.w.
 *
 * The driver argument is the address of a net_io_$driver_t (declared below);
 * it is kept as void * because both callers hold it that way -
 * ROUTE_$SERVICE in a `void *` local (0x00E6A158) and RING_$INIT as
 * "pea (0x518,A0)" off RING_$CTL (0x00E2FB84).
 *
 * Returns the index of the new port, or -1 when *status_ret is set.
 *
 * Original address: 0x00E5A4A4
 */
int16_t NET_IO_$CREATE_PORT(int16_t port_type, uint16_t unit,
                            void *driver, uint16_t queue_length,
                            status_$t *status_ret);

/*
 * NET_IO_$INIT - Initialize network I/O
 *
 * Original address: 0x00E31C98
 */
void NET_IO_$INIT(void);

/*
 * =============================================================================
 * net_io_$driver_t - the 0x50-byte network driver descriptor
 * =============================================================================
 *
 * route_$port_t.driver_info (+0x48) points at one of these; every network
 * entry point in the kernel reaches its device through it.  The record is a
 * short scalar head followed by sixteen procedure variables and a UID:
 *
 *   head    +0x00 word, +0x02 word, +0x04 word, +0x06 byte, +0x07 byte
 *   vector  +0x08 .. +0x47, sixteen longword procedure variables
 *   tail    +0x48 .. +0x4F, the eight-byte network-type UID
 *
 * Three blocks in the image have this shape and together they name every
 * slot.  Two are here; the third is the ring driver at RING_$CTL + 0x518
 * (0xE86918, handed to NET_IO_$CREATE_PORT at 0x00E2FB84), whose slots all
 * carry SAU2 map symbols and so give the fields their names:
 *
 *   off   NIL         USER                      RING (0xE86918)
 *   0x08  0           ROUTE_$SEND_USER_PORT     RING_$SENDP
 *   0x0C  0           ROUTE_$READ_USER_STATS    RING_$GET_STATS
 *   0x10  0           0                         RING_$GET_STATS
 *   0x14  0           0                         RING_$START
 *   0x18  0           0                         RING_$STOP
 *   0x1C  0           0                         0
 *   0x20  NET_IO_$CLEANUP_NIL  NET_IO_$CLEANUP_USER  RING_$PROC2_CLEANUP
 *   0x24  0           0                         RING_$IOCTL
 *   0x28  0           0                         RING_$SVC_OPEN
 *   0x2C  0           0                         RING_$SVC_CLOSE
 *   0x30  0           0                         RING_$SVC_IOCTL
 *   0x34  0           0                         RING_$SVC_WRITE
 *   0x38  0           0                         RING_$SVC_READ
 *   0x3C  0           0                         RING_$OPEN_OS
 *   0x40  0           0                         RING_$CLOSE_OS
 *   0x44  0           0                         RING_$SEND_OS
 *
 * The dispatchers, with the instruction that proves each slot:
 *   0x08  NET_IO_$SEND        "movea.l (0x8,A4),A0 / jsr (A0)"   0x00E0E892
 *   0x0C  NET_IO_$DEVICE_STAT "movea.l (0xc,A2),A1 / jsr (A1)"   0x00E5A410
 *   0x10  NET_IO_$DEVICE_STAT2 "movea.l (0x10,A2),A1 / jsr (A1)" 0x00E5A494
 *   0x14  ROUTE_$SERVICE      leaving port status 1              0x00E6A4xx
 *   0x18  ROUTE_$SERVICE      entering port status 1             0x00E6A5xx
 *   0x1C  ROUTE_$SERVICE      after the 0x14 call succeeds
 *   0x20  NET_IO_$FREE_ASID   "movea.l (0x20,A2),A0 / jsr (A0)"  0x00E74EB0
 *   0x24  NETWORK_$SET_SERVICE "movea.l (0x24,A1),A0 / jsr (A0)" 0x00E0F5E4
 *   0x28  NET_$OPEN   \                                          0x00E5A1B8
 *   0x2C  NET_$CLOSE   \  each computes its slot as
 *   0x30  NET_$IOCTL    >  "lea (0xe2451c+k).l,A0 / sub.l        0x00E5A228
 *   0x34  NET_$SEND    /    #0xe244f4,D0" and hands the
 *   0x38  NET_$RCV    /     difference to NET_$FIND_HANDLER      0x00E5A2E0
 *   0x3C  MAC_OS_$OPEN, 0x40 MAC_OS_$CLOSE, 0x44 MAC_OS_$SEND
 *          (mac_os/mac_os.h MAC_OS_DRIVER_*_OFFSET)
 *
 * The slots are procedure variables, not data: every dispatcher tests the
 * slot for zero and reports status_$network_operation_not_defined_on_hardware
 * (or simply does nothing) when it is nil.  One generic type is used for all
 * sixteen because their argument lists differ; the callers cast.
 */
typedef void (*net_io_$driver_fn_t)(void);

typedef struct net_io_$driver_t {
    uint16_t    _unknown0;          /* 0x00: 2 in all three blocks; no reader
                                     *       found in the image */
    uint16_t    max_data_len;       /* 0x02: largest data length this port will
                                     *       carry.  PKT_$BLD_INTERNET_HDR
                                     *       compares against it and against it
                                     *       plus 0x100 ("cmp.w (0x2,A0),D3w" at
                                     *       0x00E1211E, "move.w (0x2,A0),D6w /
                                     *       addi.l #0x100,D6" at 0x00E12136);
                                     *       MSG_$$SEND repeats both tests at
                                     *       0x00E0DAD0 and 0x00E0DAEC.
                                     *       NIL 0x1000, USER 0x400, RING 0x400 */
    uint16_t    mtu;                /* 0x04: MAC_OS_$INIT copies it into
                                     *       mac_os_$port_info_t.mtu
                                     *       ("move.w (0x4,A0),(0x8a2,A1)");
                                     *       NIL 0x1400, USER 0, RING 0 */
    uint8_t     _unknown6;          /* 0x06: 0 in all three blocks */
    uint8_t     flags;              /* 0x07: driver capability bits.
                                     *       ROUTE_$VALIDATE_PORT requires bit
                                     *       1 (0x02) before it will route
                                     *       through the port; NIL 0, USER 0,
                                     *       RING 3 */
    net_io_$driver_fn_t sendp;      /* 0x08: transmit a packet (NET_IO_$SEND) */
    net_io_$driver_fn_t get_stats;  /* 0x0C: NET_IO_$DEVICE_STAT */
    net_io_$driver_fn_t get_stats2; /* 0x10: NET_IO_$DEVICE_STAT2 */
    net_io_$driver_fn_t start;      /* 0x14: bring the port up (ROUTE_$SERVICE) */
    net_io_$driver_fn_t stop;       /* 0x18: take the port down (ROUTE_$SERVICE) */
    net_io_$driver_fn_t detach;     /* 0x1C: ROUTE_$SERVICE calls it right after
                                     *       a successful start; nil in all
                                     *       three blocks, so its purpose is
                                     *       inferred only from the call shape
                                     *       (port socket ptr, nil, 0, 0) */
    net_io_$driver_fn_t proc2_cleanup;/* 0x20: per-address-space teardown
                                     *       (NET_IO_$FREE_ASID) */
    net_io_$driver_fn_t ioctl;      /* 0x24: NETWORK_$SET_SERVICE's notification
                                     *       entry; RING_$IOCTL in the ring
                                     *       block.  route/route.h models the
                                     *       same slot as
                                     *       route_$driver_info_t.set_service */
    net_io_$driver_fn_t svc_open;   /* 0x28: NET_$OPEN */
    net_io_$driver_fn_t svc_close;  /* 0x2C: NET_$CLOSE */
    net_io_$driver_fn_t svc_ioctl;  /* 0x30: NET_$IOCTL */
    net_io_$driver_fn_t svc_write;  /* 0x34: NET_$SEND */
    net_io_$driver_fn_t svc_read;   /* 0x38: NET_$RCV */
    net_io_$driver_fn_t open_os;    /* 0x3C: MAC_OS_$OPEN */
    net_io_$driver_fn_t close_os;   /* 0x40: MAC_OS_$CLOSE */
    net_io_$driver_fn_t send_os;    /* 0x44: MAC_OS_$SEND */
    uid_t       network_uid;        /* 0x48: network-type UID.  NET_IO_$BOOT_DEVICE
                                     *       copies NIL_$NETWORK_UID (0xE1748C)
                                     *       here in the NIL block
                                     *       (0x00E31C28 -> 0xE2453C) and
                                     *       USER_$NETWORK_UID (0xE1749C) in the
                                     *       USER block (0x00E31C3A -> 0xE2458C);
                                     *       NET_IO_$DEVICE_STAT copies the two
                                     *       longwords out and substitutes
                                     *       UNKNOWN_$NETWORK_UID (0xE174A4)
                                     *       when the port does not exist
                                     *       ("lea (0x48,A2),A1 / move.l (A1)+"
                                     *       at 0x00E5A3EA) */
} net_io_$driver_t;

#define NET_IO_DRIVER_SIZE 0x50

/*
 * Layout checks.  The record holds procedure variables, so its size only
 * matches the image on a 32-bit-pointer target; the host test build carries
 * wider pointers and is exempt.
 */
#if defined(ARCH_M68K)
_Static_assert(offsetof(net_io_$driver_t, max_data_len)   == 0x02, "net_io_$driver_t.max_data_len");
_Static_assert(offsetof(net_io_$driver_t, mtu)            == 0x04, "net_io_$driver_t.mtu");
_Static_assert(offsetof(net_io_$driver_t, flags)          == 0x07, "net_io_$driver_t.flags");
_Static_assert(offsetof(net_io_$driver_t, sendp)          == 0x08, "net_io_$driver_t.sendp");
_Static_assert(offsetof(net_io_$driver_t, get_stats)      == 0x0C, "net_io_$driver_t.get_stats");
_Static_assert(offsetof(net_io_$driver_t, get_stats2)     == 0x10, "net_io_$driver_t.get_stats2");
_Static_assert(offsetof(net_io_$driver_t, start)          == 0x14, "net_io_$driver_t.start");
_Static_assert(offsetof(net_io_$driver_t, stop)           == 0x18, "net_io_$driver_t.stop");
_Static_assert(offsetof(net_io_$driver_t, detach)         == 0x1C, "net_io_$driver_t.detach");
_Static_assert(offsetof(net_io_$driver_t, proc2_cleanup)  == 0x20, "net_io_$driver_t.proc2_cleanup");
_Static_assert(offsetof(net_io_$driver_t, ioctl)          == 0x24, "net_io_$driver_t.ioctl");
_Static_assert(offsetof(net_io_$driver_t, svc_open)       == 0x28, "net_io_$driver_t.svc_open");
_Static_assert(offsetof(net_io_$driver_t, svc_close)      == 0x2C, "net_io_$driver_t.svc_close");
_Static_assert(offsetof(net_io_$driver_t, svc_ioctl)      == 0x30, "net_io_$driver_t.svc_ioctl");
_Static_assert(offsetof(net_io_$driver_t, svc_write)      == 0x34, "net_io_$driver_t.svc_write");
_Static_assert(offsetof(net_io_$driver_t, svc_read)       == 0x38, "net_io_$driver_t.svc_read");
_Static_assert(offsetof(net_io_$driver_t, open_os)        == 0x3C, "net_io_$driver_t.open_os");
_Static_assert(offsetof(net_io_$driver_t, close_os)       == 0x40, "net_io_$driver_t.close_os");
_Static_assert(offsetof(net_io_$driver_t, send_os)        == 0x44, "net_io_$driver_t.send_os");
_Static_assert(offsetof(net_io_$driver_t, network_uid)    == 0x48, "net_io_$driver_t.network_uid");
_Static_assert(sizeof(net_io_$driver_t) == NET_IO_DRIVER_SIZE,
               "net_io_$driver_t must be 0x50 bytes");
#endif

/*
 * NET_IO_$CLEANUP_NIL / NET_IO_$CLEANUP_USER - the proc2_cleanup slots of the
 * two software driver blocks.  NET_IO_$FREE_ASID calls the slot as
 * "pea (0x30,A4) / move.w (0x8,A6),-(SP)" (0x00E74EAC), i.e. the port's
 * socket cell by address and the address-space id by value, with a word
 * result slot that is discarded.
 *
 * (net_io/cleanup_nil.c, 0x00E74EC8, 84 bytes; net_io/cleanup_user.c,
 * 0x00E74F1E, 92 bytes.)
 */
void NET_IO_$CLEANUP_NIL(uint16_t *socket_ptr, uint16_t asid);
void NET_IO_$CLEANUP_USER(uint16_t *socket_ptr, uint16_t asid);

/*
 * NET_IO_$NIL_DRIVER / NET_IO_$USER_DRIVER - Driver descriptor blocks
 *
 * Passed (by address) to NET_IO_$CREATE_PORT by ROUTE_$SERVICE: the NIL
 * driver for port type 1 (local network ports) and the USER driver for
 * user routing ports (0x00E6A158 / 0x00E6A162).
 *
 * The SAU2 map has `D E244F0 NET_IO size = AC` holding, in order,
 * NET_IO_$ALL_F_ADDR (0xE244F0), NET_IO_$NIL_DRIVER (0xE244F4),
 * NET_IO_$USER_DRIVER (0xE24544) and RING_$OVERFLOW_OVERFLOW (0xE24594), so
 * each driver block is exactly 0x50 bytes.
 *
 * Both are declared as one-element arrays because that is how their users
 * name them: ROUTE_$SERVICE assigns the block itself to a `void *` and passes
 * it on ("driver = NET_IO_$NIL_DRIVER"), matching the image, where the symbol
 * is the block's address.
 *
 * Original addresses: 0xE244F4 (NIL), 0xE24544 (USER)
 */
extern net_io_$driver_t NET_IO_$NIL_DRIVER[1];
extern net_io_$driver_t NET_IO_$USER_DRIVER[1];

/*
 * =============================================================================
 * NET_IO_UNWIRED (0xE81668, `D E81668 NET_IO_UNWIRED size = 14`)
 * =============================================================================
 *
 * The module's whole unwired data block: eight per-port address-space ids
 * followed by the boot device the PROM handed the kernel.  It has no interior
 * symbols in the SAU2 map, so the field names are tree names.
 *
 *   0x00  port_asid[8]  NET_IO_$CREATE_PORT stores PROC1_$AS_ID for the port
 *                       it just built ("move.w (0x00e2060a).l,(0x0,A5,D0*0x1)"
 *                       at 0x00E5A5B0 and 0x00E5A688, with D0/D3 the port
 *                       index doubled); NET_IO_$CLEANUP_NIL (0x00E74EE0) and
 *                       NET_IO_$CLEANUP_USER (0x00E74F36) read it back.
 *   0x10  boot_unit     the second argument of NET_IO_$BOOT_DEVICE
 *                       ("move.w D1w,(0x10,A0)" at 0x00E31C8C).  0x3E7 (999)
 *                       in the image, the "no network boot device" sentinel
 *                       NET_IO_$CREATE_PORT tests at 0x00E5A50C.
 *   0x12  boot_port_type  0 for boot devices 2 and 3, 4 for device 6, 5 for
 *                       device 8 (0x00E31C5A / 0x00E31C6C / 0x00E31C80).
 *
 * NET_IO_$CREATE_PORT gives the port that matches (boot_port_type, boot_unit)
 * index 0, the primary network port (0x00E5A500 - 0x00E5A524).
 */
#define NET_IO_$MAX_PORTS       8

/* The value NET_IO_$BOOT_DEVICE never wrote: "no network boot device". */
#define NET_IO_$NO_BOOT_UNIT    0x3E7

typedef struct net_io_unwired_t {
    uint16_t    port_asid[NET_IO_$MAX_PORTS];   /* 0x00 */
    uint16_t    boot_unit;                      /* 0x10 */
    uint16_t    boot_port_type;                 /* 0x12 */
} net_io_unwired_t;

_Static_assert(offsetof(net_io_unwired_t, boot_unit) == 0x10,
               "net_io_unwired_t.boot_unit");
_Static_assert(offsetof(net_io_unwired_t, boot_port_type) == 0x12,
               "net_io_unwired_t.boot_port_type");
_Static_assert(sizeof(net_io_unwired_t) == 0x14,
               "NET_IO_UNWIRED must be 0x14 bytes");

extern net_io_unwired_t NET_IO_UNWIRED;

/*
 * =============================================================================
 * Status codes (names and texts from the SR10.2 status-code database)
 * =============================================================================
 */
/*
 * 0x2B0009 "operation not legal on this port type" belongs to status module
 * 0x2B (OS / internet routing) and is declared once, as
 * status_$route_illegal_op_for_port_type, in route/route.h (bead source-6vat).
 * NET_IO_$CREATE_PORT stores it at 0x00E5A4F6.
 */
#define status_$net_io_max_ports_open           0x2B0005  /* "max number of ports already open" */
#define status_$net_io_no_user_buffer_queues    0x2B000B  /* "no more buffer queues for user networks" */
#define status_$net_io_max_user_ports_open      0x2B000F  /* "max number of USER ports already open" */


/*
 * NET_IO_$DEVICE_STAT (0x00E5A39C) / NET_IO_$DEVICE_STAT2 (0x00E5A420)
 *
 * Both take the same seven arguments; the frame of the first is
 * (0x8,A6) word, (0xA,A6) word, (0xC,A6) word, (0xE,A6) long, (0x12,A6) long,
 * (0x16,A6) long, (0x1A,A6) long and its callers clean up 0x18 bytes.
 * ROUTE_$FIND_PORTP maps (network, index) to a port; if there is none the
 * routine copies a canned 8-byte reply and returns
 * status_$internet_unknown_network_port (0x00E5A3DE).
 *
 * ASKNODE_$INTERNET_INFO's request-0x3D and request-0x5B arms call them with
 * max_len = 0x80 (0x00E651BE-0x00E651FE).
 */
void NET_IO_$DEVICE_STAT(uint16_t network, uint16_t index, uint16_t max_len,
                         void *id_ret, void *stat_buf, uint16_t *stat_len_ret,
                         status_$t *status_ret);
void NET_IO_$DEVICE_STAT2(uint16_t network, uint16_t index, uint16_t max_len,
                          void *id_ret, void *stat_buf, uint16_t *stat_len_ret,
                          status_$t *status_ret);

#endif /* NET_IO_H */
