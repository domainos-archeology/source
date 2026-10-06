/*
 * AUDIT - Auditing Subsystem
 *
 * This module provides auditing services for Domain/OS including:
 * - Event logging (with SID, timestamp, and process info)
 * - Per-process audit suspension/resume
 * - Selective auditing via audit list
 * - Administrator privilege checking
 *
 * The audit subsystem logs security-relevant events to a persistent file.
 * Events include file operations, process creation, and security changes.
 *
 * Files used:
 *   //node_data/audit/audit_log  - Event log file
 *   //node_data/audit/audit_list - UID filter list (optional)
 *   //node_data/audit            - Admin rights check
 *
 * Memory layout (m68k):
 *   - AUDIT_$ENABLED: 0xE2E09E
 *   - AUDIT_$CORRUPTED: 0xE2E09C
 *   - Main data area: 0xE854D8
 */

#ifndef AUDIT_H
#define AUDIT_H

/*
 * Status codes, module 0x30 ("OS / audit trail manager" in the SR10.4
 * status-code database).  Single definition; stop/ raises it too and used to
 * spell it status_$stop_not_diag.
 */
#define status_$audit_event_logging_is_disabled 0x00300004

#include "base/base.h"

/*
 * ============================================================================
 * Initialization and Shutdown
 * ============================================================================
 */

/*
 * AUDIT_$INIT - Initialize the audit subsystem
 *
 * Initializes all audit data structures, clears suspension counters,
 * allocates the event counter and exclusion lock, and attempts to
 * start audit logging.
 *
 * If starting fails, a warning is printed and AUDIT_$CORRUPTED is set.
 * In corrupted mode, all events are logged regardless of the audit list.
 *
 * Must be called once during system startup.
 *
 * Original address: 0x00E70AFC
 */
void AUDIT_$INIT(void);

/*
 * AUDIT_$SHUTDOWN - Shutdown the audit subsystem
 *
 * Stops audit logging and flushes pending events.
 * Called during system shutdown.
 *
 * Original address: 0x00E70D1E
 */
void AUDIT_$SHUTDOWN(void);

/*
 * ============================================================================
 * Process Audit State
 * ============================================================================
 */

/*
 * AUDIT_$IS_PROCESS_AUDITED - Check if current process is being audited
 *
 * A process is audited if its suspension count is zero.
 * Suspension count is incremented by AUDIT_$SUSPEND and
 * decremented by AUDIT_$RESUME.
 *
 * Returns:
 *   Non-zero (0xFF) if process is audited, 0 if suspended
 *
 * Original address: 0x00E70D94
 */
int8_t AUDIT_$IS_PROCESS_AUDITED(void);

/*
 * AUDIT_$SUSPEND - Suspend auditing for current process
 *
 * Increments the suspension counter for the current process.
 * While suspended, events from this process are not logged.
 * Multiple suspensions are nested (counter-based).
 *
 * Original address: 0x00E70DB6
 */
void AUDIT_$SUSPEND(void);

/*
 * AUDIT_$RESUME - Resume auditing for current process
 *
 * Decrements the suspension counter for the current process.
 * Auditing resumes when the counter reaches zero.
 *
 * Original address: 0x00E70DD6
 */
void AUDIT_$RESUME(void);

/*
 * AUDIT_$INHERIT_AUDIT - Copy audit state to child process
 *
 * Copies the suspension counter from the current process to
 * the specified child process. Called during process creation.
 *
 * Parameters:
 *   child_pid  - Pointer to child process ID
 *   status_ret - Output status code (always set to 0)
 *
 * Original address: 0x00E7169C
 */
void AUDIT_$INHERIT_AUDIT(int16_t *child_pid, status_$t *status_ret);

/*
 * ============================================================================
 * Event Logging
 * ============================================================================
 */

/*
 * AUDIT_$LOG_EVENT - Log an audit event
 *
 * Logs an audit event if auditing is enabled and the current process
 * is not suspended. Retrieves the SID for the current process.
 *
 * Parameters:
 *   event_uid    - UID identifying the event type
 *   event_flags  - Pointer to event flags word
 *   sid          - SID data (retrieved internally for this variant)
 *   status       - Pointer to status value to log
 *   data         - Pointer to event-specific data
 *   data_len     - Pointer to length of data (max 2048 bytes)
 *
 * Original address: 0x00E70DF6
 */
void AUDIT_$LOG_EVENT(uid_t *event_uid, uint16_t *event_flags,
                      status_$t *status, char *data,
                      const uint16_t *data_len);

/*
 * AUDIT_$LOG_EVENT_S - Log an audit event with explicit SID
 *
 * Logs an audit event with the specified SID. This is the core
 * logging function called by AUDIT_$LOG_EVENT.
 *
 * The event record includes:
 *   - Record header (size, version)
 *   - SID data (36 bytes from sid parameter)
 *   - Event flags
 *   - Node ID
 *   - Event UID
 *   - Status value
 *   - Timestamp
 *   - Process IDs (PID, UPID)
 *   - Variable-length data
 *
 * When selective auditing is enabled (list_count > 0), only events
 * for UIDs in the audit list are logged, unless CORRUPTED is set.
 *
 * Parameters:
 *   event_uid    - UID identifying the event type
 *   event_flags  - Pointer to event flags word
 *   sid          - Pointer to SID data buffer (36 bytes)
 *   status       - Pointer to status value to log
 *   data         - Pointer to event-specific data
 *   data_len     - Pointer to length of data (max 2048 bytes)
 *
 * Original address: 0x00E70E40
 */
void AUDIT_$LOG_EVENT_S(uid_t *event_uid, uint16_t *event_flags,
                        void *sid, status_$t *status,
                        char *data, const uint16_t *data_len);

/*
 * ============================================================================
 * Administration
 * ============================================================================
 */

/*
 * AUDIT_$ADMINISTRATOR - Check if caller has audit administrator privileges
 *
 * Checks if the current process has administrative access to the audit
 * subsystem by resolving //node_data/audit and checking ACL rights.
 *
 * Parameters:
 *   status_ret - Receives operation status (0x30000C if audit file not found)
 *
 * Returns:
 *   0xFF (-1) if caller has admin rights (rights == 2)
 *   0x00 otherwise
 *
 * Original address: 0x00E714B6
 */
int8_t AUDIT_$ADMINISTRATOR(status_$t *status_ret);

/*
 * AUDIT_$CONTROL - Control audit subsystem operations
 *
 * Administrative interface for controlling the audit subsystem.
 * Requires audit administrator privileges for most operations.
 *
 * Commands:
 *   0 (LOAD_LIST)    - Reload audit list from file
 *   1 (FLUSH)        - Flush audit buffer to disk
 *   2 (START)        - Start audit logging
 *   3 (STOP)         - Stop audit logging
 *   4 (SUSPEND_SELF) - Suspend auditing for caller
 *   5 (RESUME_SELF)  - Resume auditing for caller
 *   6 (IS_ENABLED)   - Query if auditing is enabled
 *
 * Parameters:
 *   command    - Pointer to command number
 *   status_ret - Output status code
 *
 * Status codes:
 *   0x00300004 - Auditing is not enabled (for IS_ENABLED)
 *   0x00300007 - Invalid command
 *   0x00300008 - Not an audit administrator
 *   0x00300011 - Auditing is enabled and process not suspended (IS_ENABLED)
 *
 * Original address: 0x00E71534
 */
void AUDIT_$CONTROL(int16_t *command, status_$t *status_ret);

/*
 * AUDIT_$SERVER - Audit server process main loop
 *
 * Main loop for the audit server background process.
 * Waits for events and periodically flushes the audit buffer.
 *
 * This function is started as a separate process by audit_$start_logging.
 * It runs until auditing is disabled.
 *
 * Original address: 0x00E710C6
 */
void AUDIT_$SERVER(void);

/*
 * ============================================================================
 * Naming/Directory Operation Loggers
 * ============================================================================
 * These live in the audit subsystem (audit/log_*_op.c) and are called from
 * dir/ (DIR_$DO_OP, set_default_acl_internal.c).
 */

/* AUDIT_$LOG_CNAME_OP - Audit CNAMEU operation
 * Original address: 0x00E4BEC2
 */
void AUDIT_$LOG_CNAME_OP(uint16_t audit_type, status_$t status, uid_t *uid,
                         uint16_t name_len, uint16_t new_name_len,
                         void *name, void *new_name);

/* AUDIT_$LOG_LINK_OP - Audit link operation
 * target_data is a pointer (the routine copies from it with OS_$DATA_COPY).
 * Original address: 0x00E4BD48
 */
void AUDIT_$LOG_LINK_OP(uint16_t audit_type, status_$t status, uid_t *uid,
                        uint16_t name_len, void *name, uint16_t target_len,
                        void *target_data);

/* AUDIT_$LOG_DIR_OP - Audit add/drop entry operation
 * Original address: 0x00E4BE16
 */
void AUDIT_$LOG_DIR_OP(uint16_t audit_type, status_$t status, uid_t *dir_uid,
                       uid_t *file_uid, uint16_t name_len, void *name);

/* audit_$log_mount_op - Audit mount/drop mount operation
 * Original address: 0x00E4BCE0
 */
void audit_$log_mount_op(uint16_t audit_type, status_$t status, uid_t *uid,
                         uid_t *mount_uid, uint32_t extra);

/* audit_$log_prot_op - Audit protection operation
 * Original address: 0x00E4AF28
 */
void audit_$log_prot_op(status_$t status, uid_t *uid, void *prot_data,
                        uid_t *acl_uid, uid_t *subject_uid, uint16_t prot_flags);

/*
 * ============================================================================
 * Global Data - Event UIDs
 * ============================================================================
 */

/*
 * Event UIDs.  These are 8-byte cells in the image's audit event-UID table
 * at 0x00E85600: {word event_class, word subtype, longword 0}.
 */
/* 0x00E85668: class 4, subtype 7 - a process changed its SIDs.  Pushed by
 * ACL_$SET_RE_ALL_SIDS (0x00E48542) and ACL_$SET_RES_ALL_SIDS (0x00E4877A). */
extern uid_t AUDIT_$SET_SID_EU;

/* 0x00E85640: class 4, subtype 0x0E - logical volume dismounted */
extern uid_t AUDIT_$DISMOUNT_LV_EU;

/* 0x00E85648: class 4, subtype 0x0D - logical volume mounted */
extern uid_t AUDIT_$MOUNT_LV_EU;

/* 0x00E85650: class 4, subtype 0x0C - a disk was assigned.  Pushed by
 * DISK_$PV_MOUNT_INTERNAL (0x00E6C838) for every mount type. */
extern uid_t AUDIT_$ASSIGN_DISK_EU;

/* 0x00E85658: class 4, subtype 0x0B - a disk was mounted.  Selected (into a
 * local that is never passed on) by DISK_$PV_MOUNT_INTERNAL (0x00E6C7EE). */
extern uid_t AUDIT_$MOUNT_DISK_EU;

/* 0x00E85660: class 4, subtype 8 - a process entered a subsystem.  Pushed by
 * ACL_$ENTER_SUBS (0x00E46F76). */
extern uid_t AUDIT_$ENTER_SUBS_EU;

/*
 * audit_$log_resolve_op - Audit a name-resolve operation
 *
 * Only DIR_$DO_OP calls it and the body currently lives in
 * dir/audit_log_resolve_op.c, but the audit_$ prefix makes it an audit
 * export (moved here from dir/dir_internal.h -- bead source-3uo).
 *
 * Original address: 0x00E4BF92
 */
void audit_$log_resolve_op(uint32_t pname_data, uint16_t path_len,
                           void *result, status_$t status);

/* Master enable flag (0xE2E09E, defined in audit/audit_data.c) */
extern int8_t AUDIT_$ENABLED;

/*
 * The AUDIT wired data segment (map "D E2E07C AUDIT size = 20", no interior
 * symbol, so the tree name is module-local): the eventcount and exclusion
 * lock AUDIT_$INIT keeps a pointer to in AUDIT_$DATA.event_count
 * (0x00E70B0E `move.l A0,(0x198,A5)`), handed out by GET_WIRED (A0 =
 * 0xE2E07C, misc/get_wired.c).  The ML exclusion sits at +0x0C
 * (audit/init.c `event_count + 0x0C`).  Zero in the image; defined in
 * audit/audit_data.c.  Pointer-bearing, so the layout asserts are
 * target-only.
 */
#include "ec/ec.h"
#include "ml/ml.h"

#define AUDIT_WIRED_EC_SIZE 0x20        /* map: AUDIT size = 20 */

typedef struct audit_$wired_ec_t {
    ec_$eventcount_t ec;                /* +0x00 */
    ml_$exclusion_t  lock;              /* +0x0C */
    uint8_t          _1e[2];            /* +0x1E: to the segment end */
} audit_$wired_ec_t;

#if defined(ARCH_M68K)
_Static_assert(__builtin_offsetof(audit_$wired_ec_t, lock) == 0x0C, "AUDIT wired: lock at +0x0C");
_Static_assert(sizeof(audit_$wired_ec_t) == AUDIT_WIRED_EC_SIZE, "AUDIT wired: map size 0x20");
#endif

MODULE_DATA_DECLARE(audit_$wired_ec_t, audit_$wired_ec, 0x00E2E07C);

#endif /* AUDIT_H */
