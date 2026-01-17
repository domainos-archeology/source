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
                      uint32_t *status, char *data, uint16_t *data_len);

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
                        void *sid, uint32_t *status,
                        char *data, uint16_t *data_len);

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

#endif /* AUDIT_H */
