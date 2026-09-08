/*
 * XNS Internal Header
 *
 * Internal definitions, helper functions, and data structures for the
 * XNS IDP implementation. Not part of the public API.
 */

#ifndef XNS_INTERNAL_H
#define XNS_INTERNAL_H

#include "arch/arch.h"
#include "ec/ec.h"
#include "fim/fim.h"
#include "mac/mac.h"
#include "mac_os/mac_os.h"
#include "netbuf/netbuf.h"
#include "os/os.h"
#include "network/network.h"
#include "proc1/proc1.h"
#include "proc2/proc2.h"
#include "rip/rip.h"
#include "route/route.h"
#include "sock/sock.h"
#include "xns/xns.h"

/*
 * Global XNS IDP state base address
 *
 * On M68K, this is the direct hardware address.
 * On other platforms, it's an extern pointer.
 */
#if defined(ARCH_M68K)
#define XNS_IDP_BASE ((uint8_t *)0xE2B314)
#else
extern uint8_t *XNS_IDP_BASE;
#endif

/*
 * Offsets into the XNS IDP state structure (relative to 0xE2B314)
 */
#define XNS_OFF_PACKETS_SENT 0x000
#define XNS_OFF_PACKETS_RECV 0x004
#define XNS_OFF_PACKETS_DROP 0x008
#define XNS_OFF_PORT_NETWORK 0x010
/*
 * The registered local host addresses, six bytes each, at state +0x20
 * (xns_$idp_state_t.addrs).  XNS_IDP_$OS_OPEN copies the FIRST entry into a
 * connected channel's source host with three word moves at
 * 0x00E180F4-0x00E18108, and xns_$is_broadcast_addr walks the array with
 * "lea (A5),A1 / addq.l #6,A1" reading (0x20,A1) (0x22,A1) (0x24,A1).
 */
#define XNS_OFF_ADDRS 0x020
#define XNS_ADDR_SIZE 6
#define XNS_OFF_LOCAL_SOCKET 0x020
#define XNS_OFF_LOCAL_HOST_HI 0x022
#define XNS_OFF_LOCAL_HOST_LO 0x024
/*
 * XNS_OFF_REG_ADDR_BASE used to name 0x026 as the "first registered address
 * entry".  The array actually starts at 0x020 with a six-byte stride
 * (XNS_OFF_ADDRS above), so 0x026 is the SECOND entry; the name is kept only
 * because nothing referenced it and removing it silently would lose the
 * correction.
 */
#define XNS_OFF_REG_ADDR_BASE (XNS_OFF_ADDRS + XNS_ADDR_SIZE) /* addrs[1] */
#define XNS_OFF_PORTS 0x040         /* xns_$port_state_t ports[8], stride 0x0C */
/*
 * The channel array starts at state +0xA0 with a 0x48 stride.  Verified in
 * XNS_IDP_$OS_DEMUX: the socket scan walks `movea.l A5,A1' /
 * `lea (0x48,A1),A1' reading (0xD8,A0), and the demux vector is (0xA0,A2)
 * with A2 = A5 + index * 0x48.  0xA0 + 16 * 0x48 == 0x520 == XNS_OFF_LOCK.
 */
#define XNS_OFF_CHANNELS 0x0A0
#define XNS_OFF_LOCK 0x520
#define XNS_OFF_OPEN_COUNT 0x534
#define XNS_OFF_NEXT_SOCKET 0x536
#define XNS_OFF_REG_COUNT 0x538

/*
 * Channel field offsets.
 *
 * NOTE: these are A5-relative offsets for CHANNEL 0, i.e. they already
 * include the 0xA0 channel-array base.  Code that uses them must index as
 * `XNS_IDP_BASE + idx * XNS_CHANNEL_SIZE + XNS_CHAN_OFF_xxx'.  Prefer
 * XNS_CHANNEL_PTR() and the xns_$channel_t fields, whose offsets are
 * channel-relative.
 *
 * The first four entries are in fact xns_$port_state_t fields (the port
 * array at +0x40), kept here under their historical names.
 */
#define XNS_CHAN_OFF_PORT_REF 0x40
#define XNS_CHAN_OFF_PORT_INFO 0x44
#define XNS_CHAN_OFF_MAC_SOCKET 0x48
#define XNS_CHAN_OFF_PORT_REFCOUNT 0x4A
#define XNS_CHAN_OFF_DEMUX 0xA0
#define XNS_CHAN_OFF_DEST_NETWORK 0xA4
#define XNS_CHAN_OFF_DEST_HOST 0xA8
#define XNS_CHAN_OFF_DEST_SOCKET 0xAE
#define XNS_CHAN_OFF_SRC_NETWORK 0xB0
#define XNS_CHAN_OFF_SRC_HOST 0xB4
#define XNS_CHAN_OFF_SRC_PORT 0xBA
#define XNS_CHAN_OFF_MAC_INFO 0xBC
#define XNS_CHAN_OFF_CONN_PORT 0xD4
#define XNS_CHAN_OFF_USER_SOCKET 0xD6
#define XNS_CHAN_OFF_XNS_SOCKET 0xD8
#define XNS_CHAN_OFF_FLAGS 0xDA
#define XNS_CHAN_OFF_PORT_ACTIVE 0xDC
#define XNS_CHAN_OFF_STATE 0xE4

/*
 * Channel size
 */
#define XNS_CHANNEL_SIZE 0x48

/*
 * Per-port state offsets (relative to port base, 12 bytes apart)
 */
#define XNS_PORT_OFF_REF 0x40
#define XNS_PORT_OFF_INFO 0x44
#define XNS_PORT_OFF_MAC_SOCKET 0x48
#define XNS_PORT_OFF_REFCOUNT 0x4A

/*
 * Port state size
 */
#define XNS_PORT_STATE_SIZE 0x0C

/*
 * XNS_ERROR module data - the whole D segment "XNS_ERROR" at 0x00E2B29C,
 * size 0x78 (SAU2 link map).  XNS_ERROR_$SEND, xns_$maybe_open_error_socket,
 * xns_$maybe_close_error_socket and xns_$pkt_bufs_in_netbuf_pool all set
 * A5 to that address, so every displacement they use is a field here.
 *
 * The link map names two interior symbols: CLIENT_REF_COUNT at 0x00E2B310
 * (A5+0x74, module-local - no `$' in the name) and XNS_ERROR_$STD_IDP_CHANNEL
 * at 0x00E2B312 (A5+0x76).  Both are WORDS: 0x00E178CC "tst.w (0x74,A5)",
 * 0x00E178F6 "addq.w #0x1,(0x74,A5)", 0x00E178F0 "move.w (-0x26,A6),(0x76,A5)"
 * and 0x00E17946 "move.w #-0x1,(0x76,A5)".
 *
 * The head of the segment IS the request record XNS_IDP_$OS_SEND is given:
 * 0x00E17BAC pushes "pea (A5)" as that argument.  Only +0x18..+0x24 is ever
 * written (0x00E17B2C-0x00E17B38); the address block at +0x00..+0x17 is left
 * alone because the error channel builds its own IDP header.
 */
typedef struct xns_error_$data_t {
  xns_$os_send_rec_t send_rec;        /* 0x00: the OS_SEND request record */
  uint8_t  _unknown_48[0x24];         /* 0x48 */
  int32_t  buf_va_high;               /* 0x6C (A5+0x6C) */
  int32_t  buf_va_low;                /* 0x70 (A5+0x70) */
  int16_t  client_ref_count;          /* 0x74, map: CLIENT_REF_COUNT */
  int16_t  std_idp_channel;           /* 0x76, map: XNS_ERROR_$STD_IDP_CHANNEL */
} xns_error_$data_t;
/*
 * NOT packed: the natural layout already matches.  xns_$os_send_rec_t is
 * itself packed (alignment 1) so send_rec occupies 0x00..0x47 exactly,
 * _unknown_48 runs to 0x6B, and the two longwords land on 0x6C / 0x70
 * without padding.  Leaving the outer record unpacked is what lets
 * "&XNS_ERROR_$DATA.send_rec" be taken without -Waddress-of-packed-member.
 */

_Static_assert(offsetof(xns_error_$data_t, buf_va_high)      == 0x6C, "xns_error.buf_va_high");
_Static_assert(offsetof(xns_error_$data_t, buf_va_low)       == 0x70, "xns_error.buf_va_low");
_Static_assert(offsetof(xns_error_$data_t, client_ref_count) == 0x74, "xns_error.client_ref_count");
_Static_assert(offsetof(xns_error_$data_t, std_idp_channel)  == 0x76, "xns_error.std_idp_channel");
_Static_assert(sizeof(xns_error_$data_t) == 0x78,
               "the XNS_ERROR segment is 0x78 bytes (SAU2 link map)");

extern xns_error_$data_t XNS_ERROR_$DATA;

/*
 * XNS_ERROR_$CLIENT_MUTEX (0x00E26268, SAU2 link map) - guards
 * client_ref_count and std_idp_channel.  Pushed by its literal address at
 * 0x00E178BE / 0x00E178FA / 0x00E1791C / 0x00E1794C.
 */
extern ml_$exclusion_t XNS_ERROR_$CLIENT_MUTEX;

/*
 * Internal helper function declarations
 */

/*
 * xns_$find_socket - Check if a socket number is already in use
 *
 * Scans all active channels to find if the given socket number
 * is already bound to an active channel.
 *
 * @param socket    Socket number to check
 *
 * @return 0xFF (-1 as signed char) if found (in use), 0 if not found
 * (available)
 *
 * Original address: 0x00E17D12
 */
int8_t xns_$find_socket(int16_t socket);

/*
 * xns_$add_port - Add a port to a channel's port list
 *
 * Adds the specified port to the channel's active port list.
 * Opens the MAC layer if this is the first channel using this port.
 *
 * @param channel       Channel index
 * @param port          Port number
 * @param status_ret    Output: status code
 *
 * Original address: 0x00E17BF8
 */
void xns_$add_port(uint16_t channel, int16_t port, status_$t *status_ret);

/*
 * xns_$delete_port - Remove a port from a channel's port list
 *
 * Removes the specified port from the channel's active port list.
 * Closes the MAC layer if this was the last channel using this port.
 *
 * @param channel       Channel index
 * @param port          Port number
 * @param status_ret    Output: status code
 *
 * Original address: 0x00E17CB2
 */
void xns_$delete_port(uint16_t channel, int16_t port, status_$t *status_ret);

/*
 * xns_$get_checksum - compute the IDP checksum over a packet descriptor
 *
 * Reads its argument at +0x1C/+0x20/+0x24 (the {length, address, next} header
 * descriptor, walked to the end of the chain at 0x00E17D52-0x00E17DD2) and at
 * +0x38/+0x3C.. (the payload length and pages, 0x00E17DD4-0x00E17E08).  Both
 * mac_os_$send_pkt_t (XNS_IDP_$OS_SEND, 0x00E18434) and the receive
 * descriptor XNS_IDP_$OS_DEMUX is handed (0x00E184F2) have those fields at
 * those offsets, so the parameter stays untyped.
 *
 * @param packet_info   mac_os_$send_pkt_t or xns_$mac_rcv_t
 *
 * @return the checksum, or -1 when the packet is malformed
 *
 * Original address: 0x00E17D46
 */
int16_t xns_$get_checksum(void *packet_info);

/*
 * xns_$is_broadcast_addr - Check if address is a broadcast address
 *
 * Checks if the given XNS address is the broadcast address
 * (network -1, host -1, socket -1) or a registered address.
 *
 * @param addr      Pointer to 12-byte XNS address (network + host + socket)
 *
 * @return 0xFF (-1) if broadcast/local, 0 if remote
 *
 * Original address: 0x00E17E88
 */
int8_t xns_$is_broadcast_addr(void *addr);

/*
 * xns_$is_local_addr - Check if host portion is local
 *
 * Validates that the host portion of an address matches one of
 * our registered addresses.
 *
 * @param addr      Pointer to host address (6 bytes)
 *
 * @return 0xFF (-1) if broadcast (all 0xFF), 0 if OK, other on error
 *
 * Original address: 0x00E17850
 */
int8_t xns_$is_local_addr(void *addr);

/*
 * Inline accessor macros for channel state
 */
#define XNS_CHANNEL_PTR(idx)                                                   \
  ((xns_$channel_t *)(XNS_IDP_BASE + XNS_OFF_CHANNELS +                        \
                      (idx) * XNS_CHANNEL_SIZE))
#define XNS_PORT_PTR(idx)                                                      \
  ((xns_$port_state_t *)(XNS_IDP_BASE + XNS_OFF_PORTS +                        \
                         (idx) * XNS_PORT_STATE_SIZE))
#define XNS_PACKETS_RECV() (*(uint32_t *)(XNS_IDP_BASE + XNS_OFF_PACKETS_RECV))
#define XNS_PACKETS_DROP() (*(uint32_t *)(XNS_IDP_BASE + XNS_OFF_PACKETS_DROP))
#define XNS_LOCK_PTR() ((ml_$exclusion_t *)(XNS_IDP_BASE + XNS_OFF_LOCK))
#define XNS_OPEN_COUNT() (*(uint16_t *)(XNS_IDP_BASE + XNS_OFF_OPEN_COUNT))
#define XNS_NEXT_SOCKET() (*(uint16_t *)(XNS_IDP_BASE + XNS_OFF_NEXT_SOCKET))
#define XNS_REG_COUNT() (*(uint16_t *)(XNS_IDP_BASE + XNS_OFF_REG_COUNT))

#endif /* XNS_INTERNAL_H */
