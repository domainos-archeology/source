/*
 * NET_IO - Network I/O Module
 *
 * This module provides low-level network I/O operations.
 */

#ifndef NET_IO_H
#define NET_IO_H

#include "base/base.h"

/*
 * NET_IO_$SEND - Send a network packet
 *
 * Sends a packet over the network to a specified destination.
 *
 * @param port        Port/interface to send on
 * @param hdr_ptr     Pointer to header buffer pointer
 * @param hdr_pa      Header physical address
 * @param hdr_len     Header length
 * @param data_va     Data virtual address
 * @param data_len    Pointer to data length
 * @param protocol    Protocol number
 * @param flags       Send flags
 * @param extra       Extra parameters
 * @param status_ret  Output: status code
 *
 * Original address: 0x00E0E692
 */
void NET_IO_$SEND(int16_t port, uint32_t *hdr_ptr, uint32_t hdr_pa,
                  uint16_t hdr_len, uint32_t data_va, uint32_t *data_len,
                  uint16_t protocol, uint16_t flags, void *extra,
                  status_$t *status_ret);

/*
 * NET_IO_$PUT_IN_SOCK - Put packet in socket
 *
 * Original address: 0x00E0E4A0
 */
void NET_IO_$PUT_IN_SOCK(uint16_t net_type, uint16_t socket, void **hdr_ptr,
                         void **data_ptr, uint16_t hdr_len, uint16_t data_len);

/*
 * NET_IO_$COPY_PACKET - Copy a packet to network buffers
 *
 * Copies packet data from user buffers to network buffers suitable
 * for transmission.
 *
 * @param dest_addr_p   Pointer to destination address pointer
 * @param header_len    Header length
 * @param data_ptr      Pointer to packet data
 * @param flags         Flags (flags1 << 16 | flags2)
 * @param data_len      Data length
 * @param hdr_buf       Output header buffer array
 * @param data_buf      Output data buffer array
 * @param status_ret    Output status code
 *
 * Original address: 0x00E0E514
 */
void NET_IO_$COPY_PACKET(void **dest_addr_p, uint16_t header_len, void *data_ptr,
                         uint32_t flags, uint16_t data_len,
                         void **hdr_buf, void **data_buf, status_$t *status_ret);

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
