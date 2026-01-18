/*
 * PACCT - Process Accounting Subsystem
 *
 * This module provides Unix-style process accounting. When enabled,
 * it writes an accounting record for each terminated process to a
 * designated accounting file.
 *
 * Accounting records include:
 *   - User/group/org SIDs
 *   - CPU time (user + system)
 *   - Elapsed time
 *   - Memory usage (average)
 *   - I/O counts
 *   - Process UID and command name
 *   - Exit status flags
 *
 * The accounting file is memory-mapped for efficient writes. Records
 * are 128 bytes (0x80) each.
 *
 * Access Control:
 *   Starting and stopping accounting requires locksmith (superuser)
 *   privileges.
 *
 * Memory Layout (m68k):
 *   Accounting state block: 0xE817EC (32 bytes)
 */

#ifndef PACCT_H
#define PACCT_H

#include "base/base.h"

/*
 * ============================================================================
 * Constants
 * ============================================================================
 */

/* Process accounting record size */
#define PACCT_RECORD_SIZE       0x80    /* 128 bytes per record */

/* Mapped buffer size */
#define PACCT_BUFFER_SIZE       0x8000  /* 32KB mapping */

/* Status codes */
#define status_$no_rights                               0x000F0010
#define status_$insufficient_rights_to_perform_operation 0x00230002

/*
 * ============================================================================
 * Types
 * ============================================================================
 */

/*
 * Compressed accounting value (comp_t)
 *
 * Used for CPU times and other large values. Format:
 *   bits 0-12:  13-bit mantissa
 *   bits 13-15: 3-bit exponent (multiply mantissa by 8^exp)
 *
 * This allows representing values up to ~2^37 in 16 bits.
 */
typedef uint16_t comp_t;

/*
 * Process accounting record structure
 * Size: 128 bytes (0x80)
 *
 * This is written to the accounting file for each terminated process.
 */
typedef struct pacct_record_t {
    uint16_t    ac_flags;       /* 0x00: Accounting flags (fork, su, core) */
    uint8_t     ac_stat;        /* 0x02: Exit status (low 8 bits) */
    uint8_t     ac_pad1;        /* 0x03: Padding */
    uid_t       ac_uid;         /* 0x04: User SID (8 bytes) */
    uid_t       ac_gid;         /* 0x0C: Group SID (8 bytes) */
    uid_t       ac_org;         /* 0x14: Organization SID (8 bytes) */
    uid_t       ac_login;       /* 0x1C: Login SID (8 bytes) */
    uid_t       ac_prot_uid;    /* 0x24: Protection UID (8 bytes) */
    uint32_t    ac_devno;       /* 0x2C: TTY device number (-1 if none) */
    int32_t     ac_btime;       /* 0x30: Process start time (Unix epoch) */
    comp_t      ac_io_read;     /* 0x34: I/O read blocks compressed */
    comp_t      ac_io_write;    /* 0x36: I/O write blocks compressed */
    comp_t      ac_elapsed;     /* 0x38: Elapsed time compressed */
    uid_t       ac_proc_uid;    /* 0x3A: Process UID (8 bytes) */
    uint8_t     ac_pad2[4];     /* 0x42: Padding */
    comp_t      ac_utime;       /* 0x46: User CPU time compressed */
    comp_t      ac_stime;       /* 0x48: System CPU time compressed */
    comp_t      ac_mem;         /* 0x4A: Average memory usage compressed */
    uint8_t     ac_pad3[28];    /* 0x4C: Padding to offset 0x68 */
    uint8_t     ac_comm[24];    /* 0x68: Command name (up to 24 chars, padded) */
} pacct_record_t;

/*
 * ============================================================================
 * Function Prototypes
 * ============================================================================
 */

/*
 * PACCT_$INIT - Initialize the process accounting subsystem
 *
 * Sets the accounting owner to UID_$NIL and clears state variables.
 * Called during system startup.
 *
 * Original address: 0x00E31CE8
 */
void PACCT_$INIT(void);

/*
 * PACCT_$SHUTDN - Shutdown the process accounting subsystem
 *
 * If accounting is enabled:
 *   - Unmaps the accounting file buffer
 *   - Unlocks the accounting file
 *   - Clears the accounting owner
 *
 * Original address: 0x00E5A6C0
 */
void PACCT_$SHUTDN(void);

/*
 * PACCT_$START - Start process accounting
 *
 * Enables process accounting to the specified file. Requires locksmith
 * privileges. If accounting is already enabled, shuts down existing
 * accounting first.
 *
 * Parameters:
 *   file_uid   - UID of the accounting file (must be a regular file)
 *   unused     - Unused parameter
 *   status_ret - Output status code
 *
 * Status codes:
 *   status_$ok - Accounting started successfully
 *   status_$insufficient_rights_to_perform_operation - Not locksmith
 *   status_$no_rights - File is not writable
 *
 * Original address: 0x00E5A746
 */
void PACCT_$START(uid_t *file_uid, uint32_t unused, status_$t *status_ret);

/*
 * PACCT_$STOP - Stop process accounting
 *
 * Disables process accounting if currently enabled. Requires locksmith
 * privileges.
 *
 * Does not return a status - check PACCT_$ON to verify accounting
 * has stopped.
 *
 * Original address: 0x00E5A8C0
 */
void PACCT_$STOP(void);

/*
 * PACCT_$ON - Check if process accounting is enabled
 *
 * Returns:
 *   true (-1)  if accounting is enabled
 *   false (0)  if accounting is disabled
 *
 * Original address: 0x00E5A9A4
 */
boolean PACCT_$ON(void);

/*
 * PACCT_$LOG - Log a process accounting record
 *
 * Writes an accounting record for a terminated process. Called by
 * the process termination code.
 *
 * If the accounting buffer is full (< 128 bytes remaining), the
 * buffer is unmapped and a new 32KB region is mapped.
 *
 * Parameters:
 *   fork_flag     - Pointer to fork flag byte (bit 7 = forked)
 *   su_flag       - Pointer to superuser flag byte (bit 7 = used su)
 *   exit_status   - Pointer to exit status (uses byte at offset 1)
 *   start_clock   - Process start time (clock_t)
 *   proc_times    - Process times structure (user/sys times at offsets 8,12)
 *   user_time     - Pointer to user CPU time (seconds)
 *   sys_time      - Pointer to system CPU time (seconds)
 *   tty_uid       - TTY UID for device number lookup
 *   proc_uid      - Process UID
 *   comm_ptr      - Command name string
 *   comm_len      - Pointer to command name length
 *
 * Original address: 0x00E5AA9C
 */
void PACCT_$LOG(uint8_t *fork_flag, uint8_t *su_flag, int16_t *exit_status,
                clock_t *start_clock, void *proc_times,
                int32_t *user_time, int32_t *sys_time,
                uid_t *tty_uid, uid_t *proc_uid,
                char *comm_ptr, int16_t *comm_len);

#endif /* PACCT_H */
