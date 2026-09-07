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
 *   param       - low word of the boot info, stored when diskless
 *
 * Returns: -1 (0xFF) if booting over the network (diskless), 0 otherwise.
 * OS_$INIT stores the result in NETWORK_$DISKLESS.
 *
 * Original address: 0x00E31C14
 * TODO: no C implementation yet.
 */
char NET_IO_$BOOT_DEVICE(short boot_device, short param);

/*
 * NET_IO_$CREATE_PORT - Create a network I/O port
 *
 * Stack frame (0x00E5A4A4): port_type word at 8(A6), unit word at 0xA(A6),
 * driver long at 0xC(A6), queue_length word at 0x10(A6), status_ret long
 * at 0x12(A6); result returned in D0.w.
 *
 * Original address: 0x00E5A4A4
 * TODO: no C implementation yet; called from RING_$INIT and ROUTE_$SERVICE.
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
 * NET_IO_$NIL_DRIVER / NET_IO_$USER_DRIVER - Driver descriptor blocks
 *
 * Passed (by address) to NET_IO_$CREATE_PORT by ROUTE_$SERVICE: the NIL
 * driver for port type 1 (local network ports) and the USER driver for
 * user routing ports.  Layout not yet decoded.
 *
 * Original addresses: 0xE244F4 (NIL), 0xE24544 (USER)
 */
extern uint8_t NET_IO_$NIL_DRIVER[];
extern uint8_t NET_IO_$USER_DRIVER[];

#endif /* NET_IO_H */
