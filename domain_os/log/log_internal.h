/*
 * LOG - System Error Log Subsystem - Internal Header
 *
 * Internal definitions for the log subsystem including the global
 * state structure and internal helper functions.
 */

#ifndef LOG_INTERNAL_H
#define LOG_INTERNAL_H

#include "log/log.h"
#include "ml/ml.h"
#include "time/time.h"
#include "vfmt/vfmt.h"
#include "wp/wp.h"

/*
 * Log buffer header structure
 *
 * The log buffer is organized as a circular buffer with head and tail
 * indices. Each entry follows the header.
 */
typedef struct log_buffer_header_t {
    int16_t     head;               /* 0x00: Index of first valid entry */
    int16_t     tail;               /* 0x02: Index of next free slot */
    /* Entry data follows */
} log_buffer_header_t;

/*
 * Log entry header structure
 *
 * Each log entry has a fixed header followed by variable-length data.
 */
typedef struct log_entry_header_t {
    int16_t     size;               /* 0x00: Entry size in words (including header) */
    int16_t     type;               /* 0x02: Entry type code */
    uint32_t    timestamp;          /* 0x04: Timestamp from TIME_$CURRENT_CLOCKH */
    /* Entry data follows at offset 0x08 */
} log_entry_header_t;

/* =============================================================================
 * Early Log Buffer Structure
 *
 * Before LOG_$INIT completes, log entries can be stored in a pre-allocated
 * buffer at address 0x00e00000. This allows crash/boot info to be logged
 * before the full logging system is available.
 * =============================================================================
 */

/* Early log buffer at 0x00e00000 */
typedef struct early_log_t {
    uint32_t    magic;              /* 0x00: LOG_PENDING_MAGIC if valid */
    uint8_t     data[8];            /* 0x04: Crash/boot data */
} early_log_t;

/* Additional early log at 0x00e0000c */
typedef struct early_log_extended_t {
    uint32_t    magic;              /* 0x00: LOG_PENDING_MAGIC if valid */
    int16_t     data_len;           /* 0x04: Data length */
    int16_t     type;               /* 0x06: Log type */
    uint32_t    timestamp;          /* 0x08: Timestamp */
    uint8_t     data[8];            /* 0x0c: Log data */
} early_log_extended_t;

/* =============================================================================
 * Global State
 * =============================================================================
 */

/* LOG_$STATE, LOG_$LOGFILE_PTR are declared in log/log.h (pmap pokes
 * LOG_$LOGFILE_PTR directly) */

/* Convenience macro for accessing the log file UID */
#define LOG_$LOGFILE_UID        (LOG_$STATE.logfile_uid)

/* Early log buffers - fixed addresses in the original for crash recovery */
extern early_log_t          EARLY_LOG;          /* 0x00e00000 (DAT_00e00000) */
extern early_log_extended_t EARLY_LOG_EXTENDED; /* 0x00e0000c (DAT_00e0000c) */

/*
 * Zero-length data sentinel passed to LOG_$ADD for the init entry and
 * to ERROR_$PRINT by log_$check_op_status.  In the original this is the
 * byte at 0x00e2fffc (DAT_00e2fffc), just after the vfmt strings.
 */
extern uint32_t DAT_00e2fffc;

/*
 * log_$last_status - status shared with log_$check_op_status
 *
 * The original log_$check_op_status was a nested Pascal procedure that
 * read the status variable from LOG_$INIT's frame.  The flattened C
 * version reads this global instead; callers must store their status
 * here before calling log_$check_op_status.
 * TODO: pass the status explicitly once all callers are converted.
 */
extern status_$t log_$last_status;

/* =============================================================================
 * Internal Functions
 * =============================================================================
 */

/*
 * log_$check_op_status - Check operation status and report errors
 *
 * Checks the status code from a log operation. If non-zero, prints
 * an error message and returns -1 (0xFF). Otherwise returns 0.
 *
 * Parameters:
 *   op - Name of the operation for error message
 *
 * Returns:
 *   0 on success, -1 (0xFF) on error
 *
 * Original address: 00e2ff7c
 */
int8_t log_$check_op_status(const char *op);

/*
 * log_$read_internal - Internal log read implementation
 *
 * Reads log data from the mapped buffer with bounds checking.
 *
 * Parameters:
 *   buffer     - Destination buffer
 *   offset     - Byte offset into log buffer
 *   max_len    - Maximum bytes to read
 *   actual_len - Receives actual bytes read
 *
 * Original address: 00e17778
 */
void log_$read_internal(void *buffer, uint16_t offset, uint16_t max_len, uint16_t *actual_len);

/* =============================================================================
 * External Dependencies
 *
 * Note: Most external dependencies are declared through proper headers
 * included in the .c files. These are stubs for functions not yet in headers.
 * =============================================================================
 */

/* WP_$UNWIRE declared in wp/wp.h; ERROR_$PRINT declared in vfmt/vfmt.h */

/* Path to system error log file */
#define LOG_FILE_PATH   "//node_data/system_logs/sys_error"

/* Path length (24 characters) */
extern int16_t LOG_FILE_PATH_LEN;

#endif /* LOG_INTERNAL_H */
