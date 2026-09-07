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

/* Layout recovered from the disassembly -- see the field comments above. */
_Static_assert(__builtin_offsetof(log_buffer_header_t, head) == 0x00, "log_buffer_header_t.head");
_Static_assert(__builtin_offsetof(log_buffer_header_t, tail) == 0x02, "log_buffer_header_t.tail");

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

/* Layout recovered from the disassembly -- see the field comments above. */
_Static_assert(__builtin_offsetof(log_entry_header_t, size) == 0x00, "log_entry_header_t.size");
_Static_assert(__builtin_offsetof(log_entry_header_t, type) == 0x02, "log_entry_header_t.type");
_Static_assert(__builtin_offsetof(log_entry_header_t, timestamp) == 0x04, "log_entry_header_t.timestamp");

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

/* Layout recovered from the disassembly -- see the field comments above. */
_Static_assert(__builtin_offsetof(early_log_t, magic) == 0x00, "early_log_t.magic");
_Static_assert(__builtin_offsetof(early_log_t, data) == 0x04, "early_log_t.data");

/* Additional early log at 0x00e0000c */
typedef struct early_log_extended_t {
    uint32_t    magic;              /* 0x00: LOG_PENDING_MAGIC if valid */
    int16_t     data_len;           /* 0x04: Data length */
    int16_t     type;               /* 0x06: Log type */
    uint32_t    timestamp;          /* 0x08: Timestamp */
    uint8_t     data[8];            /* 0x0c: Log data */
} early_log_extended_t;

/* Layout recovered from the disassembly -- see the field comments above. */
_Static_assert(__builtin_offsetof(early_log_extended_t, magic) == 0x00, "early_log_extended_t.magic");
_Static_assert(__builtin_offsetof(early_log_extended_t, data_len) == 0x04, "early_log_extended_t.data_len");
_Static_assert(__builtin_offsetof(early_log_extended_t, type) == 0x06, "early_log_extended_t.type");
_Static_assert(__builtin_offsetof(early_log_extended_t, timestamp) == 0x08, "early_log_extended_t.timestamp");
_Static_assert(__builtin_offsetof(early_log_extended_t, data) == 0x0C, "early_log_extended_t.data");

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
 * Zero-length data sentinel passed to LOG_$ADD for the init entry and, twice
 * per call, to ERROR_$PRINT by log_$check_op_status.  In the original this is
 * the zero longword at 0x00e2fffc, just after the vfmt strings.
 */
extern uint32_t LOG_$VFMT_NO_ARG;

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
 * Nested procedure of LOG_$INIT: "op" is its one declared parameter and
 * "status" is the uplevel variable it reaches through the static link
 * ("movea.l (A6),A2" / "(-0x44,A2)"), made explicit here.
 *
 * Parameters:
 *   op     - VFMT continuation format naming the operation (ends in "%$")
 *   status - address of LOG_$INIT's status variable
 *
 * Returns:
 *   0 on success, -1 (0xFF) on error
 *
 * Original address: 00e2ff7c
 */
int8_t log_$check_op_status(const char *op, status_$t *status);

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
/*
 * Log file pathname, at 0x00e30020 in the original (36 characters, no
 * terminator; the length is a separate constant cell).  The leading "`" is
 * the Aegis node-entry-directory prefix.
 */
#define LOG_FILE_PATH   "`node_data/system_logs/sys_error_log"

/* Path length: the word 0x0024 = 36 at 0x00e3022a */
extern int16_t LOG_FILE_PATH_LEN;

#endif /* LOG_INTERNAL_H */
