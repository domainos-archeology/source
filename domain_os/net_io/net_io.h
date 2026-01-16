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
void NET_IO_$PUT_IN_SOCK(void);

/*
 * NET_IO_$COPY_PACKET - Copy a packet
 *
 * Original address: 0x00E0E514
 */
void NET_IO_$COPY_PACKET(void);

/*
 * NET_IO_$BOOT_DEVICE - Get boot device info
 *
 * Original address: 0x00E31C14
 */
void NET_IO_$BOOT_DEVICE(void);

/*
 * NET_IO_$INIT - Initialize network I/O
 *
 * Original address: 0x00E31C98
 */
void NET_IO_$INIT(void);

#endif /* NET_IO_H */
