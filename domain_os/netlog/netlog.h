/*
 * NETLOG - Network Logging Subsystem
 *
 * This module provides network logging capabilities for Domain/OS.
 * Log entries are buffered locally and sent to a remote logging server
 * when a page fills up (39 entries per page).
 *
 * The logging system supports filtering by "kind" - a bitmask that
 * controls which categories of events are logged.
 *
 * Memory layout (m68k):
 *   - NETLOG data block: 0xE85684 (base for internal state)
 *   - NETLOG_$OK_TO_LOG: 0xE248E0 (1 byte flag)
 *   - NETLOG_$OK_TO_LOG_SERVER: 0xE248E2 (1 byte flag)
 *   - NETLOG_$KINDS: 0xE248E4 (4 byte bitmask)
 *   - NETLOG_$EC: 0xE248E8 (event count, 12 bytes)
 *   - NETLOG_$NODE: 0xE248F4 (target node ID, 4 bytes)
 *   - NETLOG_$SOCK: 0xE248F8 (socket number, 2 bytes)
 */

#ifndef NETLOG_H
#define NETLOG_H

#include "base/base.h"
#include "ec/ec.h"

/*
 * Log entry kinds - these are bit positions (0-31) in NETLOG_$KINDS
 * To enable logging for a kind, set the corresponding bit.
 *
 * Bit 20 (0x100000): Server logging type 1
 * Bit 21 (0x200000): Server logging type 2
 * Other bits: Various event categories
 */
#define NETLOG_KIND_SERVER1     20      /* Server logging type 1 */
#define NETLOG_KIND_SERVER2     21      /* Server logging type 2 */

/*
 * Control commands for NETLOG_$CNTL
 */
#define NETLOG_CMD_INIT         0       /* Initialize logging */
#define NETLOG_CMD_SHUTDOWN     1       /* Shutdown logging */
#define NETLOG_CMD_UPDATE       2       /* Update kinds filter */

/*
 * Log entry structure (26 bytes, 0x1A)
 *
 * Each log entry contains a kind, process ID, timestamp, UID,
 * and up to 6 additional parameters.
 */
typedef struct netlog_entry_t {
    uint8_t     kind;           /* 0x00: Log entry type/category */
    uint8_t     process_id;     /* 0x01: Process ID that generated entry */
    uint32_t    timestamp;      /* 0x02: Clock value (high 32 bits) */
    uint32_t    uid_high;       /* 0x06: UID high word */
    uint32_t    uid_low;        /* 0x0A: UID low word */
    uint16_t    param3;         /* 0x0E: Parameter 3 */
    uint8_t     param4;         /* 0x10: Parameter 4 (low byte only) */
    uint8_t     _pad;           /* 0x11: Padding */
    uint16_t    param5;         /* 0x12: Parameter 5 */
    uint16_t    param6;         /* 0x14: Parameter 6 */
    uint16_t    param7;         /* 0x16: Parameter 7 */
    uint16_t    param8;         /* 0x18: Parameter 8 */
} netlog_entry_t;

/* Entries per page: 39 (0x27) */
#define NETLOG_ENTRIES_PER_PAGE     39

/* Entry size: 26 bytes (0x1A) */
#define NETLOG_ENTRY_SIZE           sizeof(netlog_entry_t)

/*
 * Global variables
 */

/* Flags to control logging (set by NETLOG_$CNTL) */
extern int8_t NETLOG_$OK_TO_LOG;        /* 0xE248E0: Enable general logging */
extern int8_t NETLOG_$OK_TO_LOG_SERVER; /* 0xE248E2: Enable server logging */

/* Bitmask of enabled log kinds */
extern uint32_t NETLOG_$KINDS;          /* 0xE248E4: Which kinds to log */

/* Event count - signaled when a page is ready to send */
extern ec_$eventcount_t NETLOG_$EC;     /* 0xE248E8: Page ready event */

/* Target logging server */
extern uint32_t NETLOG_$NODE;           /* 0xE248F4: Network node ID */
extern uint16_t NETLOG_$SOCK;           /* 0xE248F8: Socket number */

/*
 * ============================================================================
 * Function Prototypes
 * ============================================================================
 */

/*
 * NETLOG_$CNTL - Control network logging
 *
 * Initializes, shuts down, or updates the network logging subsystem.
 *
 * Parameters:
 *   cmd        - Command: 0=init, 1=shutdown, 2=update kinds
 *   node       - Pointer to target node ID (for init)
 *   sock       - Pointer to socket number (for init)
 *   kinds      - Pointer to kinds bitmask
 *   status_ret - Status return
 *
 * For cmd=0 (init):
 *   - Wires code/data pages
 *   - Allocates two buffer pages
 *   - Sets target node and socket
 *   - Enables logging based on kinds
 *
 * For cmd=1 (shutdown):
 *   - Sends any pending log data
 *   - Frees buffers
 *   - Unwires pages
 *   - Disables logging
 *
 * For cmd=2 (update):
 *   - Updates kinds filter
 *   - Recalculates OK_TO_LOG flags
 *
 * Original address: 0x00E71914
 */
void NETLOG_$CNTL(int16_t *cmd, uint32_t *node, uint16_t *sock,
                  uint32_t *kinds, status_$t *status_ret);

/*
 * NETLOG_$LOG_IT - Log an event
 *
 * Records a log entry if logging is enabled for the specified kind.
 * The entry is buffered and sent when the buffer fills.
 *
 * Parameters:
 *   kind    - Log entry type (bit position 0-31)
 *   uid     - Pointer to 8-byte UID
 *   param3  - Additional parameter
 *   param4  - Additional parameter (only low byte used)
 *   param5  - Additional parameter
 *   param6  - Additional parameter
 *   param7  - Additional parameter
 *   param8  - Additional parameter
 *
 * The function checks NETLOG_$KINDS to see if logging is enabled
 * for this kind. If enabled, it:
 *   1. Acquires spin lock
 *   2. Gets current timestamp
 *   3. Writes entry to buffer
 *   4. If buffer full, switches buffers and signals event count
 *   5. Releases spin lock
 *
 * Original address: 0x00E71B38
 */
void NETLOG_$LOG_IT(uint16_t kind, void *uid,
                    uint16_t param3, uint16_t param4,
                    uint16_t param5, uint16_t param6,
                    uint16_t param7, uint16_t param8);

/*
 * NETLOG_$SEND_PAGE - Send a filled log page
 *
 * Sends the completed log page to the logging server.
 * Called internally when a buffer fills, or during shutdown.
 *
 * The function:
 *   1. Captures page metadata (done count, entry count)
 *   2. Gets a network header
 *   3. Builds an internet packet header
 *   4. Sends the packet via NET_IO_$SEND
 *   5. Returns the header
 *
 * Original address: 0x00E71C78
 */
void NETLOG_$SEND_PAGE(void);

#endif /* NETLOG_H */
