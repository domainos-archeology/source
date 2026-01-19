/*
 * RINGLOG - Ring Network Logging Subsystem
 *
 * This module provides packet logging/tracing for the token ring network.
 * It maintains a circular buffer of log entries that can be used for
 * debugging and monitoring network activity.
 *
 * The logging system supports:
 * - Circular buffer with 100 entries (RINGLOG_MAX_ENTRIES)
 * - Filtering by network ID
 * - Filtering by socket type (NIL, WHO, MBX)
 * - Start/stop control
 * - Buffer retrieval for analysis
 *
 * Original memory layout (m68k):
 *   - Control data at 0xE2C32C
 *   - Ring buffer at 0xEA3E38
 */

#ifndef RINGLOG_H
#define RINGLOG_H

#include "base/base.h"

/*
 * ============================================================================
 * Constants
 * ============================================================================
 */

/* Maximum number of log entries in the circular buffer */
#define RINGLOG_MAX_ENTRIES     100

/* Size of each log entry in bytes */
#define RINGLOG_ENTRY_SIZE      0x2E    /* 46 bytes */

/* Total size of ring buffer data (index + entries) */
#define RINGLOG_BUFFER_SIZE     (2 + (RINGLOG_MAX_ENTRIES * RINGLOG_ENTRY_SIZE))

/*
 * RINGLOG_$CNTL command codes
 */
#define RINGLOG_CMD_START           0   /* Start logging, clear buffer, copy out */
#define RINGLOG_CMD_STOP_COPY       1   /* Stop logging, copy buffer out */
#define RINGLOG_CMD_COPY            2   /* Just copy buffer out (no start/stop) */
#define RINGLOG_CMD_CLEAR           3   /* Start logging, clear buffer */
#define RINGLOG_CMD_STOP            4   /* Stop logging */
#define RINGLOG_CMD_START_FILTERED  5   /* Start logging with ID filter */
#define RINGLOG_CMD_SET_NIL_SOCK    6   /* Set NIL socket filter */
#define RINGLOG_CMD_SET_WHO_SOCK    7   /* Set WHO socket filter */
#define RINGLOG_CMD_SET_MBX_SOCK    8   /* Set MBX socket filter */

/*
 * Socket type IDs for filtering
 */
#define RINGLOG_SOCK_NIL        (-1)    /* NIL socket */
#define RINGLOG_SOCK_WHO        5       /* WHO socket */
#define RINGLOG_SOCK_MBX        9       /* MBX socket */

/*
 * Log entry flag bits (at offset 0x0B in entry)
 */
#define RINGLOG_FLAG_VALID      0x04    /* Entry is valid */
#define RINGLOG_FLAG_SEND       0x02    /* Entry is for a send (vs receive) */
#define RINGLOG_FLAG_INBOUND    0x08    /* Packet was inbound */

/*
 * ============================================================================
 * Public Functions
 * ============================================================================
 */

/*
 * RINGLOG_$LOGIT - Log a packet event
 *
 * Records a packet send/receive event to the ring log buffer.
 * The event is only logged if:
 * - Logging is enabled (RING_$LOGGING_NOW)
 * - The packet's network ID matches the filter (or filter is disabled)
 * - The socket type is not filtered out
 *
 * Parameters:
 *   header_info - Pointer to packet header info (used for inbound flag)
 *   pkt_info    - Pointer to packet information structure
 *
 * Returns:
 *   Entry index on success (0-99)
 *   -1 if packet was filtered or logging disabled
 *
 * Original address: 0x00E1A20C
 */
int16_t RINGLOG_$LOGIT(uint8_t *header_info, void *pkt_info);

/*
 * RINGLOG_$CNTL - Ring logging control
 *
 * Controls the ring logging subsystem: start, stop, clear buffer,
 * set filters, and retrieve logged data.
 *
 * Parameters:
 *   cmd_ptr     - Pointer to command code (0-8)
 *   param       - Command-specific parameter:
 *                 - cmd 5: Pointer to network ID filter value
 *                 - cmd 6-8: Pointer to filter enable flag (0 = filter, -1 = don't)
 *                 - cmd 0-2: Receives buffer copy (must be RINGLOG_BUFFER_SIZE+2 bytes)
 *   status_ret  - Pointer to receive status code
 *
 * Commands:
 *   0 - Start logging, clear buffer, copy buffer to param
 *   1 - Stop logging, copy buffer to param
 *   2 - Copy buffer to param (don't start/stop)
 *   3 - Start logging, clear buffer (no copy)
 *   4 - Stop logging (no copy)
 *   5 - Start logging with ID filter, clear buffer
 *   6 - Set NIL socket filter (param = filter flag)
 *   7 - Set WHO socket filter (param = filter flag)
 *   8 - Set MBX socket filter (param = filter flag)
 *
 * Original address: 0x00E72226
 */
void RINGLOG_$CNTL(uint16_t *cmd_ptr, void *param, status_$t *status_ret);

/*
 * RINGLOG_$STOP_LOGGING - Internal: Stop logging and unwire buffer
 *
 * Stops logging, unwires any wired memory pages, and resets the
 * logging state. Called internally by RINGLOG_$CNTL.
 *
 * Original address: 0x00E721CC
 */
void RINGLOG_$STOP_LOGGING(void);

#endif /* RINGLOG_H */
