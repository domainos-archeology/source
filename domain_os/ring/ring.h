/*
 * RING - Token Ring Network Module
 *
 * This module implements the token ring network interface for Domain/OS.
 * It provides the low-level driver for Apollo's token ring hardware,
 * supporting packet transmission/reception, interrupt handling, and
 * integration with the network I/O subsystem.
 *
 * The ring subsystem manages:
 * - Up to 2 token ring units (RING_MAX_UNITS)
 * - Per-unit event counts for synchronization
 * - DMA channels for transmit and receive
 * - Socket-based channel multiplexing (up to 10 channels per unit)
 * - Statistics collection for each unit
 *
 * Hardware: Apollo DN300/DN3000 token ring controller at 0xFFA000
 * DMA channels:
 *   - Channel 0 (0xFFA000): Receive header
 *   - Channel 1 (0xFFA040): Receive data
 *   - Channel 2 (0xFFA080): Transmit
 */

#ifndef RING_H
#define RING_H

#include "base/base.h"
#include "ec/ec.h"
#include "ml/ml.h"

/*
 * ============================================================================
 * Constants
 * ============================================================================
 */

/* Maximum number of ring units */
#define RING_MAX_UNITS          2

/* Maximum number of channels per unit */
#define RING_MAX_CHANNELS       10

/* Per-unit structure size */
#define RING_UNIT_SIZE          0x244

/* Per-unit statistics structure size */
#define RING_STATS_SIZE         0x3C

/* Maximum data length for packets */
#define RING_MAX_DATA_LEN       0x400   /* 1024 bytes */

/* Network header size */
#define RING_HDR_SIZE           0x1C    /* 28 bytes */

/*
 * ============================================================================
 * Status Codes (module 0x31 = RING)
 * ============================================================================
 */
#define status_$ring_invalid_unit_num               0x00310002
#define status_$ring_illegal_header_length          0x00310003
#define status_$ring_invalid_data_length            0x00310004
#define status_$ring_socket_already_open            0x00310006
#define status_$ring_too_many_args                  0x00310009
#define status_$ring_invalid_svc_packet_type        0x00310009
#define status_$ring_channel_not_open               0x0031000A
#define status_$ring_device_offline                 0x0031000B
#define status_$ring_request_denied                 0x0031000E
#define status_$ring_invalid_ioctl                  0x00310001

#define status_$io_controller_not_in_system         0x00100002
#define status_$internet_unknown_network_port       0x002B0003

#define status_$network_transmit_failed             0x00110004
#define status_$network_data_length_too_large       0x0011001C
#define status_$network_memory_parity_error_during_transmit 0x00110016

/*
 * ============================================================================
 * Transmit Status Flags
 * ============================================================================
 */

/* Transmit result flags (returned in status byte) */
#define RING_TX_FLAG_SUCCESS        0x80    /* Transmission successful */
#define RING_TX_FLAG_COLLISION      0x40    /* Collision detected */
#define RING_TX_FLAG_NO_RESPONSE    0x20    /* No response from destination */
#define RING_TX_FLAG_ABORT          0x08    /* Transmission aborted */
#define RING_TX_FLAG_RETRY          0x04    /* Retry required */
#define RING_TX_FLAG_ERROR          0x02    /* General error */
#define RING_TX_FLAG_TIMEOUT        0x01    /* Timeout occurred */

/* Extended status flags (second byte) */
#define RING_TX_EXT_PARITY          0x80    /* Parity error */
#define RING_TX_EXT_PROTOCOL        0x40    /* Protocol error */
#define RING_TX_EXT_BIPHASE         0x20    /* Biphase error */
#define RING_TX_EXT_NOT_IN_SYSTEM   0x10    /* Controller not in system */
#define RING_TX_EXT_CONGESTION      0x08    /* Network congestion */
#define RING_TX_EXT_ESB             0x04    /* ESB error */

/*
 * ============================================================================
 * Unit Flags
 * ============================================================================
 */

/* Unit state flags (at offset +0x31 in unit structure) */
#define RING_UNIT_STARTED       0x01    /* Unit has been started */
#define RING_UNIT_RUNNING       0x02    /* Unit is actively running */
#define RING_UNIT_BUSY          0x04    /* Unit is busy with I/O */

/*
 * ============================================================================
 * Public Data
 * ============================================================================
 */

 /* Ring network data - defined as ring_global_t in ring_internal.h
  * For raw data access, cast to appropriate type */
extern char RING_$DATA[];
extern uint32_t RING_$SWDIAG_DATA;
extern uint32_t RING_$SWDIAG_RCVCNT;
extern uint32_t RING_$SWDIAG_NODEID;
extern uint16_t RING_$XMIT_BIPHASE;
extern uint16_t RING_$RCV_BIPHASE;
extern uint16_t RING_$XMIT_ESB;
extern uint16_t RING_$RCV_ESB;

/* Network UID for ring interface */
extern uid_t RING_$NETWORK_UID;

/*
 * ============================================================================
 * Statistics Structure
 * ============================================================================
 */

/*
 * Per-unit statistics (0x3C bytes)
 * Located at 0xE261E0 + (unit * 0x3C)
 */
typedef struct ring_$stats_t {
    uint16_t    _reserved0;         /* 0x00 */
    uint32_t    xmit_count;         /* 0x02: Total transmit attempts */
    uint32_t    success_count;      /* 0x06: Successful transmissions */
    uint16_t    no_response_count;  /* 0x0A: No response errors */
    uint16_t    collision_count;    /* 0x0C: Collisions detected */
    uint16_t    abort_count;        /* 0x0E: Aborted transmissions */
    uint16_t    noresp_count;       /* 0x10: No response count */
    uint16_t    parity_count;       /* 0x12: Parity errors */
    uint16_t    delayed_count;      /* 0x14: Delayed responses */
    uint16_t    biphase_count;      /* 0x16: Biphase errors */
    uint16_t    unexpected_count;   /* 0x18: Unexpected status */
    uint16_t    retry_count;        /* 0x1A: Retry attempts */
    uint8_t     _reserved1[0x18];   /* 0x1C-0x33 */
    int8_t      last_success;       /* 0x34: Last transmission succeeded */
    int8_t      _reserved2;         /* 0x35 */
    int8_t      congestion_flag;    /* 0x36: Network congestion */
    int8_t      _reserved3;         /* 0x37 */
    int8_t      biphase_flag;       /* 0x38: Biphase error flag */
    int8_t      _reserved4;         /* 0x39 */
    int8_t      retry_pending;      /* 0x3A: Retry is pending */
    int8_t      _reserved5;         /* 0x3B */
} ring_$stats_t;

/*
 * ============================================================================
 * Public Functions
 * ============================================================================
 */

/*
 * RING_$INIT - Initialize a ring unit
 *
 * Initializes the ring unit data structures, event counts, and exclusion
 * locks. Creates a network I/O port for the unit.
 *
 * @param device_info   Device information structure (from DCTE)
 *
 * @return status_$ok on success, error code on failure
 *
 * Original address: 0x00E2FAE0
 */
status_$t RING_$INIT(void *device_info);

/*
 * RING_$GET_ID - Get the network ID for a ring unit
 *
 * Retrieves the 24-bit network ID from the device's hardware address.
 * The ID is constructed from bytes at offsets 0x12, 0x14, and 0x16
 * in the device initialization data.
 *
 * @param param   Pointer to unit info
 *
 * @return Network ID (24-bit value)
 *
 * Original address: 0x00E2FA28
 */
uint32_t RING_$GET_ID(void *param);

/*
 * RING_$INT - Interrupt handler for ring unit
 *
 * Handles receive interrupts from the token ring hardware.
 * Advances event counts to wake waiting processes.
 *
 * @param device_info   Device information structure
 *
 * @return 0xFF (interrupt handled)
 *
 * Original address: 0x00E75748
 */
int8_t RING_$INT(void *device_info);

/*
 * RING_$START - Start a ring unit
 *
 * Activates a ring unit for transmission and reception.
 * Must be called after RING_$INIT.
 *
 * @param unit_ptr      Pointer to unit number
 * @param status_ret    Output: status code
 *
 * Original address: 0x00E76830
 */
void RING_$START(uint16_t *unit_ptr, status_$t *status_ret);

/*
 * RING_$STOP - Stop a ring unit
 *
 * Deactivates a ring unit. The unit can be restarted with RING_$START.
 *
 * @param unit_ptr      Pointer to unit number
 * @param status_ret    Output: status code
 *
 * Original address: 0x00E769C4
 */
void RING_$STOP(uint16_t *unit_ptr, status_$t *status_ret);

/*
 * RING_$SENDP - Send a packet on the ring
 *
 * Transmits a packet over the token ring network. Handles retries,
 * timeout management, and error reporting.
 *
 * @param unit_ptr      Pointer to unit number
 * @param hdr_pa        Header physical address
 * @param hdr_va        Header virtual address
 * @param data_info     Data buffer info (PA in high 32 bits, VA in low)
 * @param data_len      Data length
 * @param send_flags    Send flags output
 * @param result_flags  Result flags output
 * @param status_ret    Output: status code
 *
 * Original address: 0x00E75916
 */
void RING_$SENDP(uint16_t *unit_ptr, uint32_t hdr_pa, void *hdr_va,
                 uint64_t data_info, uint16_t data_len,
                 uint16_t *send_flags, uint16_t *result_flags,
                 status_$t *status_ret);

/*
 * RING_$GET_STATS - Get statistics for a ring unit
 *
 * Copies the per-unit statistics to the provided buffer.
 *
 * @param unit_ptr      Pointer to unit number
 * @param stats_buf     Output buffer for statistics (0x3C bytes)
 * @param unused        Unused parameter
 * @param len_out       Output: bytes copied
 * @param status_ret    Output: status code
 *
 * Original address: 0x00E76950
 */
void RING_$GET_STATS(uint16_t *unit_ptr, void *stats_buf, uint16_t unused,
                     uint16_t *len_out, status_$t *status_ret);

/*
 * RING_$IOCTL - I/O control for ring unit
 *
 * Performs I/O control operations on a ring unit.
 * Currently supports setting the transmit mask.
 *
 * @param unit_ptr      Pointer to unit number
 * @param cmd           Command (0 = set tmask)
 * @param param         Command parameter
 * @param status_ret    Output: status code
 *
 * Original address: 0x00E76B2C
 */
void RING_$IOCTL(uint16_t *unit_ptr, int16_t *cmd, void *param,
                 status_$t *status_ret);

/*
 * RING_$SET_TMASK - Set transmit mask
 *
 * Sets the transmit mask register for a ring unit.
 *
 * @param unit          Unit number
 * @param mask          Transmit mask value
 *
 * Original address: 0x00E768E8
 */
void RING_$SET_TMASK(uint16_t unit, uint16_t mask);

/*
 * RING_$KICK_DRIVER - Kick the ring driver
 *
 * Forces the driver to re-check for pending work.
 *
 * Original address: 0x00E768A8
 */
void RING_$KICK_DRIVER(void);

/*
 * ============================================================================
 * Service Functions (called via NET_IO dispatch)
 * ============================================================================
 */

/*
 * RING_$SVC_OPEN - Open a ring channel (service call)
 *
 * @param name          Channel name
 * @param args          Open arguments
 * @param unused        Unused
 * @param status_ret    Output: status code
 *
 * Original address: 0x00E76DF2
 */
void RING_$SVC_OPEN(void *name, void *args, void *unused, status_$t *status_ret);

/*
 * RING_$SVC_CLOSE - Close a ring channel (service call)
 *
 * @param unit_ptr      Pointer to unit number
 * @param args          Close arguments
 * @param status_ret    Output: status code
 *
 * Original address: 0x00E76E22
 */
void RING_$SVC_CLOSE(uint16_t *unit_ptr, void *args, status_$t *status_ret);

/*
 * RING_$SVC_READ - Read from a ring channel (service call)
 *
 * @param unit_ptr      Pointer to unit number
 * @param result        Result buffer
 * @param param3        Additional parameter
 * @param param4        Additional parameter
 * @param param5        Additional parameter
 * @param len_out       Output: bytes read
 * @param status_ret    Output: status code
 *
 * Original address: 0x00E77402
 */
void RING_$SVC_READ(uint16_t *unit_ptr, void *result, void *param3,
                    void *param4, uint16_t param5, int16_t *len_out,
                    status_$t *status_ret);

/*
 * RING_$SVC_WRITE - Write to a ring channel (service call)
 *
 * @param unit_ptr      Pointer to unit number
 * @param hdr           Packet header
 * @param param3        Additional parameter
 * @param data          Data buffer
 * @param data_len      Data length
 * @param result        Result output
 * @param status_ret    Output: status code
 *
 * Original address: 0x00E76F9E
 */
void RING_$SVC_WRITE(uint16_t *unit_ptr, void *hdr, void *param3,
                     void *data, int16_t data_len, uint16_t *result,
                     status_$t *status_ret);

/*
 * RING_$SVC_IOCTL - IOCTL for ring channel (service call)
 *
 * @param unit_ptr      Pointer to unit number
 * @param cmd_args      Command and arguments
 * @param param3        Additional parameter
 * @param param4        Additional parameter
 * @param status_ret    Output: status code
 *
 * Original address: 0x00E776B8
 */
void RING_$SVC_IOCTL(uint16_t *unit_ptr, void *cmd_args, void *param3,
                     void *param4, status_$t *status_ret);

/*
 * ============================================================================
 * OS-level Functions (called by network manager)
 * ============================================================================
 */

/*
 * RING_$OPEN_OS - Open ring for OS use
 *
 * @param param1        Parameter 1
 * @param args          Arguments
 * @param status_ret    Output: status code
 *
 * Original address: 0x00E77BA0
 */
void RING_$OPEN_OS(uint16_t param1, void *args, status_$t *status_ret);

/*
 * RING_$CLOSE_OS - Close ring OS use
 *
 * @param param1        Parameter 1
 * @param args          Arguments
 * @param status_ret    Output: status code
 *
 * Original address: 0x00E77C24
 */
void RING_$CLOSE_OS(uint16_t param1, void *args, status_$t *status_ret);

/*
 * RING_$SEND_OS - Send via ring for OS
 *
 * @param param1        Parameter 1
 * @param param2        Parameter 2
 * @param param3        Parameter 3
 * @param param4        Parameter 4
 * @param param5        Parameter 5
 * @param param6        Parameter 6
 * @param param7        Parameter 7
 * @param status_ret    Output: status code
 *
 * Original address: 0x00E77C60
 */
void RING_$SEND_OS(void *param1, void *param2, void *param3, void *param4,
                   void *param5, void *param6, void *param7,
                   status_$t *status_ret);

/*
 * ============================================================================
 * Receive Functions
 * ============================================================================
 */

/*
 * RING_$RCV0 - Receive daemon for unit 0
 *
 * Main receive loop for ring unit 0. Runs as a separate process.
 *
 * Original address: 0x00E76642
 */
void RING_$RCV0(void);

/*
 * RING_$RCV1 - Receive daemon for unit 1
 *
 * Main receive loop for ring unit 1. Runs as a separate process.
 *
 * Original address: 0x00E7665E
 */
void RING_$RCV1(void);

/*
 * RING_$RCV_FROM_UNIT_PRIV - Privileged receive loop
 *
 * Internal receive loop implementation for a specific unit.
 *
 * @param unit          Unit number
 *
 * Original address: 0x00E76048
 */
void RING_$RCV_FROM_UNIT_PRIV(uint16_t unit);

/*
 * RING_$POLL_STICKY_BPHERR - Poll for sticky biphase errors
 *
 * Checks for persistent biphase errors on the ring.
 *
 * @param param1        Parameter 1
 * @param param2        Parameter 2
 *
 * Original address: 0x00E76290
 */
void RING_$POLL_STICKY_BPHERR(void *param1, void *param2);

/*
 * RING_$PROC2_CLEANUP - Process cleanup handler
 *
 * Called when a process using ring sockets terminates.
 *
 * @param param1        Parameter 1
 *
 * Original address: 0x00E76A42
 */
void RING_$PROC2_CLEANUP(void *param1);

#endif /* RING_H */
