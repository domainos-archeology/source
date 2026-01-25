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

/* =============================================================================
 * Log Global State Structure
 *
 * Located at address 0x00e2b280 in the original binary.
 * This structure contains all global state for the logging subsystem.
 * =============================================================================
 */
typedef struct log_state_t {
    uid_t       logfile_uid;        /* 0x00: UID of the log file */
    int16_t    *current_entry_ptr;  /* 0x08: Pointer to current entry in buffer */
    uint16_t    spin_lock;          /* 0x0c: Spin lock for concurrent access */
    uint16_t    pad_0e;             /* 0x0e: Padding */
    uint32_t    wired_handle;       /* 0x10: Handle from MST_$WIRE */
    int16_t    *logfile_ptr;        /* 0x14: Pointer to mapped log buffer */
    int8_t      dirty_flag;         /* 0x18: Log has been modified */
    int8_t      pad_19[3];          /* 0x19: Padding to word boundary */
} log_state_t;

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

/* Global log state - address 0x00e2b280 */
extern log_state_t LOG_$STATE;

/* Convenience macros for accessing state fields */
#define LOG_$LOGFILE_UID        (LOG_$STATE.logfile_uid)
#define LOG_$LOGFILE_PTR        (LOG_$STATE.logfile_ptr)

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

/* From wp/ subsystem - not in wp.h yet */
extern void WP_$UNWIRE(uint32_t handle);

/* ERROR_$PRINT declared in vfmt/vfmt.h */

/* Path to system error log file */
#define LOG_FILE_PATH   "//node_data/system_logs/sys_error"

/* Path length (24 characters) */
extern int16_t LOG_FILE_PATH_LEN;

#endif /* LOG_INTERNAL_H */
