/*
 * IIC (I2C - Inter-Integrated Circuit) Bus Interface
 *
 * Public API for the IIC subsystem. The IIC bus is a two-wire serial
 * communication protocol used for communicating with peripheral devices.
 *
 * Note: In this system configuration, the IIC hardware is not present.
 * All functions are stubs that return status_$iic_device_not_in_system.
 */

#ifndef IIC_H
#define IIC_H

#include "base/base.h"

/* =============================================================================
 * IIC Network UIDs
 * =============================================================================
 */
extern uid_t IIC_$NETWORK_UID;          /* 0xE17484: IIC network UID */
extern uid_t NIL_$NETWORK_UID;          /* 0xE1748C: Nil network UID */
extern uid_t USER_$NETWORK_UID;         /* 0xE1749C: User network UID */

/* =============================================================================
 * IIC Status Codes (module 0x2C)
 * =============================================================================
 */
#define status_$iic_device_not_in_system    0x2c000a

/* =============================================================================
 * IIC Function Prototypes
 * =============================================================================
 */

/*
 * IIC_$INIT - Initialize the IIC subsystem
 *
 * Performs initialization of the IIC bus controller. In this system
 * configuration, this is a no-op stub.
 *
 * Original address: 00e70a54
 */
void IIC_$INIT(void);

/*
 * IIC_$ACQUIRE - Acquire an IIC device
 *
 * Attempts to acquire exclusive access to an IIC device.
 *
 * Parameters:
 *   device_id  - Identifier of the IIC device to acquire
 *   status_ret - Returns status code
 *
 * Returns:
 *   status_$iic_device_not_in_system (stubbed)
 *
 * Original address: 00e70a56
 */
void IIC_$ACQUIRE(uint32_t device_id, status_$t *status_ret);

/*
 * IIC_$RELEASE - Release an IIC device
 *
 * Releases exclusive access to a previously acquired IIC device.
 *
 * Parameters:
 *   device_id  - Identifier of the IIC device to release
 *   status_ret - Returns status code
 *
 * Returns:
 *   status_$iic_device_not_in_system (stubbed)
 *
 * Original address: 00e70a68
 */
void IIC_$RELEASE(uint32_t device_id, status_$t *status_ret);

/*
 * IIC_$STATISTICS - Get IIC device statistics
 *
 * Retrieves statistics for an IIC device.
 *
 * Parameters:
 *   device_id  - Identifier of the IIC device
 *   stats_buf  - Buffer to receive statistics data
 *   status_ret - Returns status code
 *
 * Returns:
 *   status_$iic_device_not_in_system (stubbed)
 *
 * Original address: 00e70a7a
 */
void IIC_$STATISTICS(uint32_t device_id, void *stats_buf, status_$t *status_ret);

/*
 * IIC_$SELF_TEST - Perform self-test on IIC device
 *
 * Performs a self-test on an IIC device.
 *
 * Parameters:
 *   device_id   - Identifier of the IIC device
 *   test_params - Test parameters
 *   status_ret  - Returns status code
 *
 * Returns:
 *   status_$iic_device_not_in_system (stubbed)
 *
 * Original address: 00e70a8c
 */
void IIC_$SELF_TEST(uint32_t device_id, uint32_t test_params, status_$t *status_ret);

/*
 * IIC_$SEND - Send data over IIC bus
 *
 * Sends data to an IIC device.
 *
 * Parameters:
 *   device_id  - Identifier of the IIC device
 *   address    - Target address on the IIC bus
 *   data       - Pointer to data to send
 *   length     - Length of data to send
 *   flags      - Operation flags
 *   status_ret - Returns status code
 *
 * Returns:
 *   status_$iic_device_not_in_system (stubbed)
 *
 * Original address: 00e70a9e
 */
void IIC_$SEND(uint32_t device_id, uint32_t address, void *data,
               uint32_t length, uint32_t flags, status_$t *status_ret);

/*
 * IIC_$EXISTS - Check if IIC hardware exists
 *
 * Checks whether the IIC bus controller hardware is present in the system.
 *
 * Returns:
 *   false (0) - IIC hardware not present (stubbed)
 *
 * Original address: 00e70ab0
 */
boolean IIC_$EXISTS(void);

/*
 * IIC_$RECEIVE - Receive data from IIC bus
 *
 * Receives data from an IIC device.
 *
 * Parameters:
 *   device_id       - Identifier of the IIC device
 *   address         - Source address on the IIC bus
 *   bytes_requested - Number of bytes to receive (in/out)
 *   buffer          - Buffer to receive data
 *   bytes_received  - Actual bytes received (out)
 *   status_ret      - Returns status code
 *
 * Returns:
 *   status_$iic_device_not_in_system (stubbed)
 *   bytes_requested and bytes_received are set to 0
 *
 * Original address: 00e70aba
 */
void IIC_$RECEIVE(uint32_t device_id, uint32_t address, uint16_t *bytes_requested,
                  void *buffer, uint16_t *bytes_received, status_$t *status_ret);

/*
 * IIC_$RECEIVE_CHECK - Check if data available on IIC bus
 *
 * Checks whether there is data available to receive from an IIC device.
 *
 * Parameters:
 *   device_id  - Identifier of the IIC device
 *   status_ret - Returns status code
 *
 * Returns:
 *   false (0) - No data available (stubbed)
 *   status_$iic_device_not_in_system via status_ret
 *
 * Original address: 00e70ade
 */
boolean IIC_$RECEIVE_CHECK(uint32_t device_id, status_$t *status_ret);

#endif /* IIC_H */
