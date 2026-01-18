/*
 * LOG - System Error Log Subsystem
 *
 * Public API for the system error logging subsystem. The log is stored
 * as a circular buffer in a memory-mapped file at:
 *   //node_data/system_logs/sys_error
 *
 * Log entries contain:
 *   - Entry size (in words)
 *   - Entry type code
 *   - Timestamp (from TIME_$CURRENT_CLOCKH)
 *   - Variable-length data
 *
 * The log uses spin locks for concurrent access protection.
 */

#ifndef LOG_H
#define LOG_H

#include "base/base.h"
#include "uid/uid.h"

/* =============================================================================
 * Log Constants
 * =============================================================================
 */

/* Log buffer size in bytes (1KB) */
#define LOG_BUFFER_SIZE         0x400

/* Maximum entry size in words (including header) */
#define LOG_MAX_ENTRY_WORDS     0x64

/* Maximum log index (0x1FE = 510 words = 1020 bytes, leaving room for header) */
#define LOG_MAX_INDEX           0x1FE

/* Magic value indicating pending early log entry */
#define LOG_PENDING_MAGIC       0xABCDEF01  /* -0x543210FF */

/* =============================================================================
 * Log Entry Types
 * =============================================================================
 */

/* Log entry type codes */
#define LOG_TYPE_INIT           0   /* System initialization */
#define LOG_TYPE_SHUTDOWN       4   /* System shutdown */
#define LOG_TYPE_CRASH          5   /* System crash info */

/* =============================================================================
 * Log Function Prototypes
 * =============================================================================
 */

/*
 * LOG_$INIT - Initialize the logging subsystem
 *
 * Resolves or creates the log file, maps it to memory, locks it,
 * and wires the memory for reliable access. Also processes any
 * early log entries that were queued before initialization.
 *
 * Original address: 00e30048
 */
void LOG_$INIT(void);

/*
 * LOG_$SHUTDN - Shutdown the logging subsystem
 *
 * Adds a shutdown entry to the log, unwires and unmaps the memory,
 * and unlocks the log file.
 *
 * Original address: 00e1758c
 */
void LOG_$SHUTDN(void);

/*
 * LOG_$UPDATE - Check and return log update status
 *
 * Returns the wired page handle if the log has been modified
 * since the last update call, otherwise returns 0.
 *
 * Returns:
 *   Wired page handle if log was modified, 0 otherwise
 *
 * Original address: 00e1760c
 */
uint32_t LOG_$UPDATE(void);

/*
 * LOG_$ADD - Add an entry to the system log
 *
 * Adds a timestamped entry to the circular log buffer. The entry
 * includes a type code and variable-length data. Uses spin locks
 * for thread safety.
 *
 * Parameters:
 *   type     - Entry type code (see LOG_TYPE_* constants)
 *   data     - Pointer to entry data
 *   data_len - Length of data in bytes
 *
 * Original address: 00e1763e
 */
void LOG_$ADD(int16_t type, void *data, int16_t data_len);

/*
 * LOG_$READ - Read log entries from the beginning
 *
 * Reads log data starting from offset 0.
 *
 * Parameters:
 *   buffer     - Buffer to receive log data
 *   max_len    - Pointer to maximum bytes to read (updated with actual)
 *   actual_len - Receives actual bytes read
 *
 * Original address: 00e17800
 */
void LOG_$READ(void *buffer, uint16_t *max_len, uint16_t *actual_len);

/*
 * LOG_$READ2 - Read log entries with offset
 *
 * Reads log data starting from a specified offset.
 *
 * Parameters:
 *   buffer     - Buffer to receive log data
 *   offset     - Byte offset into log buffer to start reading
 *   max_len    - Maximum bytes to read
 *   actual_len - Receives actual bytes read
 *
 * Original address: 00e17828
 */
void LOG_$READ2(void *buffer, uint16_t offset, uint16_t max_len, uint16_t *actual_len);

#endif /* LOG_H */
