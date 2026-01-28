/*
 * XNS Internal Header
 *
 * Internal definitions, helper functions, and data structures for the
 * XNS IDP implementation. Not part of the public API.
 */

#ifndef XNS_INTERNAL_H
#define XNS_INTERNAL_H

#include "ec/ec.h"
#include "fim/fim.h"
#include "mac/mac.h"
#include "mac_os/mac_os.h"
#include "netbuf/netbuf.h"
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
#define XNS_OFF_LOCAL_SOCKET 0x020
#define XNS_OFF_LOCAL_HOST_HI 0x022
#define XNS_OFF_LOCAL_HOST_LO 0x024
#define XNS_OFF_REG_ADDR_BASE 0x026 /* First registered address entry */
#define XNS_OFF_CHANNELS 0x000      /* Channel 0 starts at base */
#define XNS_OFF_LOCK 0x520
#define XNS_OFF_OPEN_COUNT 0x534
#define XNS_OFF_NEXT_SOCKET 0x536
#define XNS_OFF_REG_COUNT 0x538

/*
 * Channel structure offsets (relative to channel base)
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
 * Internal error socket channel
 * Used by XNS_ERROR_$SEND for sending error packets.
 */
extern int32_t XNS_ERROR_$STD_IDP_CHANNEL;

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
 * xns_$get_checksum - Calculate checksum from packet info
 *
 * Extracts and validates the IDP packet checksum.
 *
 * @param packet_info   Packet information structure
 *
 * @return Calculated checksum, or computed checksum from packet
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
 * xns_$copy_header - Copy IDP header to packet buffer
 *
 * Copies source/destination addresses and other header fields
 * to the packet buffer being constructed.
 *
 * @param packet_info   Source packet information
 *
 * @return Packet type byte
 *
 * Original address: 0x00E17876
 */
uint8_t xns_$copy_header(void *packet_info);

/*
 * xns_$copy_packet_data - Copy packet data to user buffer
 *
 * Copies received packet data to the user's receive buffer(s).
 *
 * @param iov_chain     I/O vector chain pointer
 * @param length        Number of bytes to copy
 *
 * Original address: 0x00E18C5E
 */
void xns_$copy_packet_data(void *iov_chain, uint16_t length);

/*
 * Inline accessor macros for channel state
 */
#define XNS_CHANNEL_PTR(idx)                                                   \
  ((xns_$channel_t *)(XNS_IDP_BASE + (idx) * XNS_CHANNEL_SIZE))
#define XNS_LOCK_PTR() ((ml_$exclusion_t *)(XNS_IDP_BASE + XNS_OFF_LOCK))
#define XNS_OPEN_COUNT() (*(uint16_t *)(XNS_IDP_BASE + XNS_OFF_OPEN_COUNT))
#define XNS_NEXT_SOCKET() (*(uint16_t *)(XNS_IDP_BASE + XNS_OFF_NEXT_SOCKET))
#define XNS_REG_COUNT() (*(uint16_t *)(XNS_IDP_BASE + XNS_OFF_REG_COUNT))

#endif /* XNS_INTERNAL_H */
