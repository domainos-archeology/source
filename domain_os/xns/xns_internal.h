/*
 * XNS Internal Header
 *
 * Internal definitions, helper functions, and data structures for the
 * XNS IDP implementation. Not part of the public API.
 *
 * Module data blocks XNS_IDP_$DATA / XNS_ERROR_$DATA: Claude Opus 5.5
 * (source-iq58).
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
 * The IDP module block is XNS_IDP_$DATA (xns/xns.h): every cell the IDP
 * routines reach (off,A5) with A5 = 0xE2B314 is a field of it, and the
 * channel and port tables are its channels[] / ports[] arrays.
 */

/*
 * XNS_ERROR_$DATA - the XNS_ERROR module block, the whole D segment
 * "XNS_ERROR" at 0x00E2B29C, size 0x78 (SAU2 link map), a MODULE_DATA block
 * linked in the map's order (the address is its ordering key, not its link
 * address; definition with the image contents in xns/xns_data.c).  XNS_ERROR_$SEND, xns_$maybe_open_error_socket,
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

MODULE_DATA_DECLARE(xns_error_$data_t, XNS_ERROR_$DATA, 0x00E2B29C);

/*
 * XNS_ERROR_$CLIENT_MUTEX (0x00E26268, SAU2 link map) - guards
 * client_ref_count and std_idp_channel.  Pushed by its literal address at
 * 0x00E178BE / 0x00E178FA / 0x00E1791C / 0x00E1794C.  It lies in the
 * RIP_WIRED segment, so it is RIP_$WIRED_DATA.xns_error_mutex (rip/rip.h).
 */

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

#endif /* XNS_INTERNAL_H */
