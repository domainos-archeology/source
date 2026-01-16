/*
 * AUDIT - Auditing Subsystem
 *
 * This module provides auditing services for Domain/OS including:
 * - Administrator privilege checking
 * - Audit event logging
 *
 * Memory layout (m68k):
 *   - See individual functions for addresses
 */

#ifndef AUDIT_H
#define AUDIT_H

#include "base/base.h"

/*
 * ============================================================================
 * Audit Functions
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
 * AUDIT_$LOG_EVENT - Log an audit event
 *
 * Logs an audit event if auditing is enabled.
 *
 * Parameters:
 *   event_id - Event identifier
 *   flags    - Event flags
 *   status   - Status to log
 *   data     - Event-specific data
 *   info     - Additional info
 *
 * Original address: 0x00E70DF6
 */
void AUDIT_$LOG_EVENT(void *event_id, void *flags, void *status,
                      void *data, void *info);

#endif /* AUDIT_H */
