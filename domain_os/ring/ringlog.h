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
 * Ring Log Control Structure
 *
 * Contains configuration and state for the logging subsystem.
 * Located at base 0xE2C32C on original platform.
 * ============================================================================
 */
typedef struct ringlog_ctl_t {
    /*
     * Wired page addresses for the ring buffer.
     * Up to some number of pages can be wired to keep buffer in physical memory.
     * Index 0 is unused; entries 1..wire_count contain wired addresses.
     */
    uint32_t    wired_pages[10];        /* 0x00: Wired page addresses (indices 1-9 used) */

    /*
     * Spinlock for buffer access.
     * Protects current_index and entry writes.
     */
    uint32_t    spinlock;               /* 0x28: Spinlock (at 0xE2C354) */

    /*
     * Network ID filter.
     * If non-zero, only packets matching this network ID are logged.
     */
    uint32_t    filter_id;              /* 0x2C: RINGLOG_$ID filter (at 0xE2C358) */

    /*
     * Number of wired pages.
     * Pages wired_pages[1] through wired_pages[wire_count] are wired.
     */
    int16_t     wire_count;             /* 0x30: Number of wired pages (at 0xE2C35C) */

    /*
     * Socket type filters.
     * When >= 0, packets to/from that socket type are NOT logged.
     * When < 0, filtering for that socket type is disabled.
     */
    int8_t      mbx_sock_filter;        /* 0x32: MBX socket filter (at 0xE2C35E) */
    int8_t      _pad1;
    int8_t      who_sock_filter;        /* 0x34: WHO socket filter (at 0xE2C360) */
    int8_t      _pad2;
    int8_t      nil_sock_filter;        /* 0x36: NIL socket filter (at 0xE2C362) */
    int8_t      _pad3;

    /*
     * Logging active flag.
     * -1 (0xFF) = logging is active
     * 0 = logging is stopped
     */
    int8_t      logging_active;         /* 0x38: RING_$LOGGING_NOW (at 0xE2C364) */
    int8_t      _pad4;

    /*
     * First entry flag.
     * Set to -1 when buffer wraps or is cleared; reset after first entry.
     * Used to detect if buffer has wrapped.
     */
    int8_t      first_entry_flag;       /* 0x3A: (at 0xE2C366) */

} ringlog_ctl_t;

/* Layout recovered from the disassembly -- see the field comments above. */
_Static_assert(__builtin_offsetof(ringlog_ctl_t, wired_pages) == 0x00, "ringlog_ctl_t.wired_pages");
_Static_assert(__builtin_offsetof(ringlog_ctl_t, spinlock) == 0x28, "ringlog_ctl_t.spinlock");
_Static_assert(__builtin_offsetof(ringlog_ctl_t, filter_id) == 0x2C, "ringlog_ctl_t.filter_id");
_Static_assert(__builtin_offsetof(ringlog_ctl_t, wire_count) == 0x30, "ringlog_ctl_t.wire_count");
_Static_assert(__builtin_offsetof(ringlog_ctl_t, mbx_sock_filter) == 0x32, "ringlog_ctl_t.mbx_sock_filter");
_Static_assert(__builtin_offsetof(ringlog_ctl_t, who_sock_filter) == 0x34, "ringlog_ctl_t.who_sock_filter");
_Static_assert(__builtin_offsetof(ringlog_ctl_t, nil_sock_filter) == 0x36, "ringlog_ctl_t.nil_sock_filter");
_Static_assert(__builtin_offsetof(ringlog_ctl_t, logging_active) == 0x38, "ringlog_ctl_t.logging_active");
_Static_assert(__builtin_offsetof(ringlog_ctl_t, first_entry_flag) == 0x3A, "ringlog_ctl_t.first_entry_flag");

/*
 * Ring log control structure.
 * On m68k, located at 0xE2C32C.  RINGLOG_$CTL.logging_active is the global
 * Ghidra labels RING_$LOGGING_NOW at 0x00E2C364; it is read from outside the
 * RING subsystem (ROUTE_$PROCESS 0x00E87602, 0x00E0E258, 0x00E0E83C,
 * 0x00E75458), which is why the record is declared in this public header.
 */
extern ringlog_ctl_t RINGLOG_$CTL;

/*
 * RING_$LOGGING_NOW - Pascal boolean (0xFF true) at 0x00E2C364.
 * Test it with "< 0", as the original does ("tst.b" / "bpl" at 0x00E87602).
 */
#define RING_$LOGGING_NOW       (RINGLOG_$CTL.logging_active)

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

/*
 * RINGLOG_$ROUTE_FORWARD - the 4-byte RINGLOG_$LOGIT header-info cell the
 * routing forwarder passes.  Only byte 0 is read, and only its bit 7 (the
 * "inbound" flag), at 0x00E1A2F6.  The storage is defined in
 * route/route_data.c (moved here from route/route_internal.h --
 * bead source-3uo).
 *
 * Original address: 0xE878A0
 */
#if defined(ARCH_M68K)
#define RINGLOG_$ROUTE_FORWARD  ((uint8_t *)0xE878A0)
#else
extern uint8_t RINGLOG_$ROUTE_FORWARD[4];
#endif

#endif /* RINGLOG_H */
