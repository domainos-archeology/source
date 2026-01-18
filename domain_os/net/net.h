/*
 * NET_$ - Network Device Abstraction Layer
 *
 * Provides a unified interface to different network hardware types
 * (Ethernet, Token Ring, etc.) through a dispatch mechanism.
 * Operations are routed to device-specific handlers based on
 * network ID and port number.
 */

#ifndef NET_NET_H
#define NET_NET_H

#include "os/os.h"

/*
 * Status codes
 */
#define status_$internet_unknown_network_port           0x002B0003
#define status_$network_operation_not_defined_on_hardware 0x0011001D

/*
 * Handler table offsets (relative to base 0xE244F4)
 * These are calculated as: &handler_address - 0xE244F4
 */
#define NET_HANDLER_OFF_OPEN    0x28    /* 0xE2451C - 0xE244F4 */
#define NET_HANDLER_OFF_CLOSE   0x2C    /* 0xE24520 - 0xE244F4 */
#define NET_HANDLER_OFF_IOCTL   0x30    /* 0xE24524 - 0xE244F4 */
#define NET_HANDLER_OFF_SEND    0x34    /* 0xE24528 - 0xE244F4 */
#define NET_HANDLER_OFF_RCV     0x38    /* 0xE2452C - 0xE244F4 */

/*
 * Public API Functions
 */

/*
 * NET_$GET_INFO - Get network information
 *
 * Currently returns status_$network_operation_not_defined_on_hardware
 * as this operation is not implemented.
 */
void NET_$GET_INFO(void *net_id, void *port, void *param3, void *param4,
                   status_$t *status_ret);

/*
 * NET_$OPEN - Open a network connection
 *
 * Looks up the appropriate device handler and calls its OPEN routine.
 * Registers cleanup handler (type 10) on success.
 *
 * Parameters:
 *   net_id     - Network identifier
 *   port       - Port number
 *   param3-5   - Device-specific parameters
 *   status_ret - Status return
 */
void NET_$OPEN(int16_t *net_id, int16_t *port, void *param3, void *param4,
               void *param5, status_$t *status_ret);

/*
 * NET_$CLOSE - Close a network connection
 *
 * Looks up the appropriate device handler and calls its CLOSE routine.
 */
void NET_$CLOSE(int16_t *net_id, int16_t *port, void *param3, void *param4,
                void *param5, status_$t *status_ret);

/*
 * NET_$IOCTL - Network I/O control
 *
 * Looks up the appropriate device handler and calls its IOCTL routine.
 */
void NET_$IOCTL(int16_t *net_id, int16_t *port, void *param3, void *param4,
                void *param5, status_$t *status_ret);

/*
 * NET_$SEND - Send data on network
 *
 * Looks up the appropriate device handler and calls its SEND routine.
 */
void NET_$SEND(int16_t *net_id, int16_t *port, void *param3, int16_t *param4,
               void *param5, void *param6, int16_t *param7, void *param8,
               status_$t *status_ret);

/*
 * NET_$RCV - Receive data from network
 *
 * Looks up the appropriate device handler and calls its RCV routine.
 */
void NET_$RCV(int16_t *net_id, int16_t *port, void *param3, int16_t *param4,
              void *param5, void *param6, int16_t *param7, void *param8,
              status_$t *status_ret);

#endif /* NET_NET_H */
