/*
 * AUDIT Internal Header - Audit Subsystem Internal Implementation
 *
 * This file contains internal data structures and functions for the audit
 * subsystem. Only audit implementation files should include this header.
 *
 * The audit subsystem provides:
 * - Event logging to persistent storage
 * - Process audit suspension/resume
 * - Audit list (selective UID auditing)
 * - Administrator privilege checking
 *
 * Memory layout (m68k):
 *   - Base data area: 0xE854D8 (accessed via A5 register)
 *   - AUDIT_$ENABLED: 0xE2E09E (global flag)
 *   - AUDIT_$CORRUPTED: 0xE2E09C (error flag)
 */

#ifndef AUDIT_INTERNAL_H
#define AUDIT_INTERNAL_H

#include "audit/audit.h"
#include "proc1/proc1.h"
#include "proc2/proc2.h"
#include "ml/ml.h"
#include "mmap/mmap.h"  /* For GET_WIRED */
#include "ec/ec.h"
#include "file/file_internal.h"

/*
 * ============================================================================
 * Status Codes
 * ============================================================================
 */
/*
 * Names taken from the SR10.4 status-code database ("OS / audit trail
 * manager", subsystem 0x30).
 */
#define status_$audit_excessive_event_types         0x00300003
#define status_$audit_event_logging_is_disabled     0x00300004
#define status_$audit_invalid_action_code           0x00300007
#define status_$audit_permission_denied             0x00300008
#define status_$audit_file_not_found                0x0030000C
#define status_$audit_event_logging_already_started 0x0030000E
#define status_$audit_event_logging_already_stopped 0x0030000F
#define status_$audit_event_list_not_current_format 0x00300010
#define status_$audit_logging_is_enabled            0x00300011

/*
 * ============================================================================
 * Constants
 * ============================================================================
 */

/* Maximum number of processes that can be tracked for audit suspension */
/*
 * Number of per-process audit suspension counters.
 *
 * NOT PROC1_MAX_PROCESSES.  AUDIT_$INIT clears exactly 0x40 words
 * (00e70b4e `moveq #0x3f,D0` ... `dbf`) starting at AUDIT_$DATA+0, and
 * AUDIT_$INIT then writes log_file_uid at +0x80 (00e70b3e
 * `move.l (A0)+,(0x80,A5)`), so the array is 64 words wide, not 65.
 *
 * The array is Pascal 1-based: AUDIT_$SUSPEND indexes it as
 * `(-0x2,A0,D0w*0x1)` with D0 = PROC1_$CURRENT*2 (00e70dc8), i.e. element
 * (pid - 1).  Entries therefore cover PIDs 1..64.
 */
#define AUDIT_MAX_PROCESSES     64

/* Hash table size for audit list UIDs */
#define AUDIT_HASH_TABLE_SIZE   37

/* Maximum data size for audit event records */
#define AUDIT_MAX_DATA_SIZE     0x800   /* 2048 bytes */

/* Buffer mapping size */
#define AUDIT_BUFFER_MAP_SIZE   0x8000  /* 32KB */

/* Default flush timeout in 4-second units (0x1E0 = 480 = 8 minutes) */
#define AUDIT_DEFAULT_TIMEOUT   0x1E0

/* Audit list format version */
#define AUDIT_LIST_VERSION_MAX  1

/* Maximum UIDs in audit list */
#define AUDIT_MAX_LIST_ENTRIES  0x100   /* 256 */

/* Process creation flags for audit server */
#define AUDIT_SERVER_PROCESS_FLAGS  0x1400000E

/*
 * ============================================================================
 * Audit Control Commands (for AUDIT_$CONTROL)
 * ============================================================================
 */
#define AUDIT_CTRL_LOAD_LIST        0   /* Load audit list from file */
#define AUDIT_CTRL_FLUSH            1   /* Flush audit buffer to disk */
#define AUDIT_CTRL_START            2   /* Start audit logging */
#define AUDIT_CTRL_STOP             3   /* Stop audit logging */
/*
 * The two self-control codes are the way round the jump table at 0x00E715F8
 * says, not the way the names would suggest.  Entry to AUDIT_$CONTROL always
 * does suspend_count++ (0x00E715D4) and its exit always does suspend_count--
 * (0x00E71688), so the net effect of each code is:
 *   4 -> table entry 0x0044 = 0x00E7163C, "if count > 1 then count--": net -1
 *        (floored at zero), i.e. RESUME
 *   5 -> table entry 0x005C = 0x00E71654, "count++": net +1, i.e. SUSPEND
 * A non-zero count means AUDIT_$IS_PROCESS_AUDITED returns false, so a higher
 * count is more suspended.
 */
#define AUDIT_CTRL_RESUME_SELF      4   /* Resume auditing for caller */
#define AUDIT_CTRL_SUSPEND_SELF     5   /* Suspend auditing for caller */
#define AUDIT_CTRL_IS_ENABLED       6   /* Query if auditing is enabled */

/*
 * ============================================================================
 * Audit Flags (stored in audit_data_t.flags at offset 0xA8)
 * ============================================================================
 */
#define AUDIT_FLAG_SELECTIVE    0x0001  /* Only audit UIDs in list */
#define AUDIT_FLAG_TIMEOUT      0x0002  /* Periodic flush enabled */

/*
 * ============================================================================
 * Data Structures
 * ============================================================================
 */

/*
 * Audit hash node - linked list entry for audit list UIDs
 *
 * Used when selective auditing is enabled. Only events for UIDs
 * in this hash table are logged.
 *
 * Size: 12 bytes (3 longs)
 */
typedef struct audit_hash_node_t {
    struct audit_hash_node_t *next;     /* 0x00: Next node in chain */
    uint32_t uid_high;                  /* 0x04: UID high word */
    uint32_t uid_low;                   /* 0x08: UID low word */
} audit_hash_node_t;

/* Layout recovered from the disassembly -- see the field comments above. */
#if defined(ARCH_M68K)
_Static_assert(__builtin_offsetof(audit_hash_node_t, next) == 0x00, "audit_hash_node_t.next");
_Static_assert(__builtin_offsetof(audit_hash_node_t, uid_high) == 0x04, "audit_hash_node_t.uid_high");
_Static_assert(__builtin_offsetof(audit_hash_node_t, uid_low) == 0x08, "audit_hash_node_t.uid_low");
_Static_assert(sizeof(audit_hash_node_t) == 0x0C, "audit_hash_node_t size");
#endif

/*
 * Audit event record header
 *
 * This is the header written to the audit log for each event.
 * The actual record size is variable (header + data).
 *
 * Size: 0x46 bytes (70 bytes) + variable data
 */
typedef struct audit_event_record_t {
    uint16_t record_size;       /* 0x00: Total record size in bytes */
    uint16_t version;           /* 0x02: Record version (always 1) */
    uint8_t  sid_data[36];      /* 0x04: SID data (9 uint32_t values) */
    uint16_t event_flags;       /* 0x28: Event flags */
    uint32_t node_id;           /* 0x2A: Node ID (upper 20 bits) */
    uid_t    event_uid;         /* 0x2E: Event UID */
    uint32_t status;            /* 0x36: Event status */
    clock_t  timestamp;         /* 0x3A: Event timestamp */
    int16_t  process_id;        /* 0x40: Process ID (level 1) */
    int16_t  upid_high;         /* 0x42: UPID high word */
    int16_t  upid_low;          /* 0x44: UPID low word */
    /* Variable-length data follows at 0x46 */
} audit_event_record_t;

/* Layout recovered from the disassembly -- see the field comments above. */
/* The recovered offsets put 32-bit fields on odd word boundaries, which
 * only m68k's 2-byte alignment reproduces.  The record cannot be marked
 * packed because callers take the address of those members
 * (-Waddress-of-packed-member), so the layout is asserted on m68k only. */
#if defined(ARCH_M68K)
_Static_assert(__builtin_offsetof(audit_event_record_t, record_size) == 0x00, "audit_event_record_t.record_size");
_Static_assert(__builtin_offsetof(audit_event_record_t, version) == 0x02, "audit_event_record_t.version");
_Static_assert(__builtin_offsetof(audit_event_record_t, sid_data) == 0x04, "audit_event_record_t.sid_data");
_Static_assert(__builtin_offsetof(audit_event_record_t, event_flags) == 0x28, "audit_event_record_t.event_flags");
_Static_assert(__builtin_offsetof(audit_event_record_t, node_id) == 0x2A, "audit_event_record_t.node_id");
_Static_assert(__builtin_offsetof(audit_event_record_t, event_uid) == 0x2E, "audit_event_record_t.event_uid");
_Static_assert(__builtin_offsetof(audit_event_record_t, status) == 0x36, "audit_event_record_t.status");
_Static_assert(__builtin_offsetof(audit_event_record_t, timestamp) == 0x3A, "audit_event_record_t.timestamp");
_Static_assert(__builtin_offsetof(audit_event_record_t, process_id) == 0x40, "audit_event_record_t.process_id");
_Static_assert(__builtin_offsetof(audit_event_record_t, upid_high) == 0x42, "audit_event_record_t.upid_high");
_Static_assert(__builtin_offsetof(audit_event_record_t, upid_low) == 0x44, "audit_event_record_t.upid_low");
_Static_assert(sizeof(audit_event_record_t) == 0x46, "audit_event_record_t size");
#endif

/*
 * Audit list file header
 *
 * Format of //node_data/audit/audit_list file.
 * Followed by array of uid_t entries.
 */
typedef struct audit_list_header_t {
    uid_t    list_uid;          /* 0x00: List file UID */
    uint16_t timeout_units;     /* 0x08: Flush timeout (in 4-sec units) */
    uint16_t version;           /* 0x0A: Format version (must be <= 1) */
    uint16_t entry_count;       /* 0x0C: Number of UID entries */
    uint16_t flags;             /* 0x0E: Audit flags */
    /* uid_t entries[entry_count] follows at 0x10 */
} audit_list_header_t;

/* Layout recovered from the disassembly -- see the field comments above. */
_Static_assert(__builtin_offsetof(audit_list_header_t, list_uid) == 0x00, "audit_list_header_t.list_uid");
_Static_assert(__builtin_offsetof(audit_list_header_t, timeout_units) == 0x08, "audit_list_header_t.timeout_units");
_Static_assert(__builtin_offsetof(audit_list_header_t, version) == 0x0A, "audit_list_header_t.version");
_Static_assert(__builtin_offsetof(audit_list_header_t, entry_count) == 0x0C, "audit_list_header_t.entry_count");
_Static_assert(__builtin_offsetof(audit_list_header_t, flags) == 0x0E, "audit_list_header_t.flags");

/*
 * Audit subsystem data area
 *
 * Main data structure for the audit subsystem.
 * Base address on m68k: 0xE854D8
 *
 * Fields extend to 0x1A2 (server_running, the last byte AUDIT_$INIT touches
 * at 00e70b1e `clr.b (0x1a2,A5)`).  The record is longword aligned, so the
 * C sizeof is 0x1A4; nothing in the image references 0xE8567B..0xE8567C.
 */
typedef struct audit_data_t {
    /* Per-process audit suspension counters (indexed by PID) */
    /* When > 0, auditing is suspended for that process */
    /* Pascal 1-based: element i holds the counter for PID i+1 */
    int16_t suspend_count[AUDIT_MAX_PROCESSES]; /* 0x00-0x7F (128 bytes) */

    /* Audit log file information */
    uid_t    log_file_uid;          /* 0x80-0x87: Audit log file UID */
    void    *buffer_base;           /* 0x88-0x8B: Mapped buffer base */
    uint32_t buffer_size;           /* 0x8C-0x8F: Mapped buffer size */
    void    *write_ptr;             /* 0x90-0x93: Current write position */
    uint32_t bytes_remaining;       /* 0x94-0x97: Bytes left in buffer */
    uint32_t file_offset;           /* 0x98-0x9B: Current file offset */
    uint8_t  dirty;                 /* 0x9C: Buffer has unwritten data */
    uint8_t  pad1[3];               /* 0x9D-0x9F: Padding */

    /* Audit list information */
    uid_t    list_uid;              /* 0xA0-0xA7: Audit list file UID */
    uint16_t flags;                 /* 0xA8-0xA9: Audit flags */
    int16_t  timeout;               /* 0xAA-0xAB: Flush timeout */
    int16_t  list_count;            /* 0xAC-0xAD: Number of UIDs in list */
    int16_t  pad2;                  /* 0xAE-0xAF: Padding */

    /* Hash table for audit list UIDs */
    audit_hash_node_t *hash_buckets[AUDIT_HASH_TABLE_SIZE]; /* 0xB0-0x143 */

    /* Padding to reach offset 0x198 */
    uint8_t  pad3[84];              /* 0x144-0x197 */

    /* Event counter and synchronization */
    ec_$eventcount_t *event_count;  /* 0x198-0x19B: Wired event counter */
    uint32_t lock_id;               /* 0x19C-0x19F: File lock ID */

    /* Audit server process */
    int16_t  server_pid;            /* 0x1A0-0x1A1: Server process ID */
    uint8_t  server_running;        /* 0x1A2: Server is running */
} audit_data_t;

/* Layout recovered from the disassembly -- see the field comments above. */
#if defined(ARCH_M68K)
_Static_assert(__builtin_offsetof(audit_data_t, suspend_count) == 0x00, "audit_data_t.suspend_count");
_Static_assert(__builtin_offsetof(audit_data_t, log_file_uid) == 0x80, "audit_data_t.log_file_uid");
_Static_assert(__builtin_offsetof(audit_data_t, buffer_base) == 0x88, "audit_data_t.buffer_base");
_Static_assert(__builtin_offsetof(audit_data_t, buffer_size) == 0x8C, "audit_data_t.buffer_size");
_Static_assert(__builtin_offsetof(audit_data_t, write_ptr) == 0x90, "audit_data_t.write_ptr");
_Static_assert(__builtin_offsetof(audit_data_t, bytes_remaining) == 0x94, "audit_data_t.bytes_remaining");
_Static_assert(__builtin_offsetof(audit_data_t, file_offset) == 0x98, "audit_data_t.file_offset");
_Static_assert(__builtin_offsetof(audit_data_t, dirty) == 0x9C, "audit_data_t.dirty");
_Static_assert(__builtin_offsetof(audit_data_t, pad1) == 0x9D, "audit_data_t.pad1");
_Static_assert(__builtin_offsetof(audit_data_t, list_uid) == 0xA0, "audit_data_t.list_uid");
_Static_assert(__builtin_offsetof(audit_data_t, flags) == 0xA8, "audit_data_t.flags");
_Static_assert(__builtin_offsetof(audit_data_t, timeout) == 0xAA, "audit_data_t.timeout");
_Static_assert(__builtin_offsetof(audit_data_t, list_count) == 0xAC, "audit_data_t.list_count");
_Static_assert(__builtin_offsetof(audit_data_t, pad2) == 0xAE, "audit_data_t.pad2");
_Static_assert(__builtin_offsetof(audit_data_t, hash_buckets) == 0xB0, "audit_data_t.hash_buckets");
_Static_assert(__builtin_offsetof(audit_data_t, pad3) == 0x144, "audit_data_t.pad3");
_Static_assert(__builtin_offsetof(audit_data_t, event_count) == 0x198, "audit_data_t.event_count");
_Static_assert(__builtin_offsetof(audit_data_t, lock_id) == 0x19C, "audit_data_t.lock_id");
_Static_assert(__builtin_offsetof(audit_data_t, server_pid) == 0x1A0, "audit_data_t.server_pid");
_Static_assert(__builtin_offsetof(audit_data_t, server_running) == 0x1A2, "audit_data_t.server_running");
_Static_assert(sizeof(audit_data_t) == 0x1A4,
               "audit_data_t: fields end at 0x1A2, longword aligned to 0x1A4");
#endif

/*
 * ============================================================================
 * Global Variables
 * ============================================================================
 *
 * Original m68k addresses documented for reference.
 */

/*
 * AUDIT_$ENABLED - Master enable flag for audit subsystem
 *
 * Set to 0xFF when auditing is enabled, 0 when disabled.
 * Original address: 0xE2E09E
 */
extern int8_t AUDIT_$ENABLED;

/*
 * AUDIT_$CORRUPTED - Error flag indicating audit subsystem is corrupted
 *
 * Set to 0xFF if initialization failed or unrecoverable error occurred.
 * When set, all events are logged regardless of audit list.
 * Original address: 0xE2E09C
 */
extern int8_t AUDIT_$CORRUPTED;

/*
 * AUDIT_$DATA - Main audit subsystem data area
 *
 * Original address: 0xE854D8
 */
extern audit_data_t AUDIT_$DATA;

/*
 * ============================================================================
 * Internal Helper Functions
 * ============================================================================
 */

/*
 * audit_$start_logging - Start audit event logging
 *
 * Loads the audit list, opens the log file, and starts the server process.
 * Called during initialization and by AUDIT_$CONTROL with CTRL_START.
 *
 * Parameters:
 *   status_ret - Output status code
 *
 * Original address: 0x00E70C78
 */
void audit_$start_logging(status_$t *status_ret);

/*
 * audit_$stop_logging - Stop audit event logging
 *
 * Flushes any pending data and stops the server process.
 *
 * Parameters:
 *   status_ret - Output status code
 *
 * Original address: 0x00E70D38
 */
void audit_$stop_logging(status_$t *status_ret);

/*
 * audit_$load_list - Load audit list from file
 *
 * Loads the audit list from //node_data/audit/audit_list.
 * The list specifies which UIDs should be audited (when selective mode).
 *
 * Parameters:
 *   status_ret - Output status code
 *
 * Returns:
 *   Non-zero (0xFF) if list was loaded successfully, 0 otherwise
 *
 * Original address: 0x00E7131C
 */
int8_t audit_$load_list(status_$t *status_ret);

/*
 * audit_$open_log - Open or create audit log file
 *
 * Opens //node_data/audit/audit_log for writing.
 * Creates the file if it doesn't exist.
 * Maps a buffer for efficient writing.
 *
 * Parameters:
 *   status_ret - Output status code
 *
 * Original address: 0x00E716CC
 */
void audit_$open_log(status_$t *status_ret);

/*
 * audit_$close_log - Close audit log file
 *
 * Flushes pending data, truncates file to actual size,
 * unmaps buffer, and releases lock.
 *
 * Parameters:
 *   status_ret - Output status code
 *
 * Original address: 0x00E7185E
 */
void audit_$close_log(status_$t *status_ret);

/*
 * audit_$clear_hash_table - Clear the audit list hash table
 *
 * Frees all hash nodes and clears bucket pointers.
 *
 * Original address: 0x00E7128A
 */
void audit_$clear_hash_table(void);

/*
 * audit_$add_to_hash - Add a UID to the audit list hash table
 *
 * Parameters:
 *   uid        - Pointer to UID to add
 *   status_ret - Output status code
 *
 * Original address: 0x00E712BA
 */
void audit_$add_to_hash(uid_t *uid, status_$t *status_ret);

/*
 * audit_$alloc - Allocate memory for audit structures
 *
 * Allocates memory from the audit memory pool.
 *
 * Parameters:
 *   size       - Number of bytes to allocate
 *   status_ret - Output status code
 *
 * Returns:
 *   Pointer to allocated memory, or NULL on failure
 *
 * Original address: 0x00E7120C
 */
void *audit_$alloc(uint16_t size, status_$t *status_ret);

/*
 * audit_$free - Free memory to audit memory pool
 *
 * Parameters:
 *   ptr - Pointer to memory to free (may be NULL)
 *
 * Original address: (part of 0x00E7120C with size=0)
 */
void audit_$free(void *ptr);

/*
 * Hash modulus for audit list (value: 37)
 */
extern int16_t AUDIT_HASH_MODULO;

#endif /* AUDIT_INTERNAL_H */
