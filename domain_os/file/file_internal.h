/*
 * FILE - File Operations Module (Internal Header)
 *
 * Internal types, constants, and helper functions used only within
 * the FILE subsystem.
 */

#ifndef FILE_INTERNAL_H
#define FILE_INTERNAL_H

#include "file/file.h"
#include "uid/uid.h"
#include "ast/ast.h"
#include "acl/acl.h"
#include "time/time.h"
#include "hint/hint.h"
#include "vtoce/vtoce.h"
#include "rem_file/rem_file.h"

/*
 * ============================================================================
 * Memory Addresses (m68k)
 * ============================================================================
 */

#ifdef M68K_TARGET
/* Lock control block base address */
#define FILE_LOCK_CONTROL_ADDR      0xE82128

/* Lock table base address (58 entries × 300 bytes) */
#define FILE_LOCK_TABLE_ADDR        0xE9F9CC

/* Lock entries base address (1792 entries × 28 bytes) */
#define FILE_LOCK_ENTRIES_ADDR      0xE935CC

/* UID lock eventcount address */
#define FILE_UID_LOCK_EC_ADDR       0xE2C028

/* Secondary table address (58 words) */
#define FILE_LOCK_TABLE2_ADDR       0xEA3DC4
#endif

/*
 * ============================================================================
 * Internal Data Structures
 * ============================================================================
 */

/* Secondary lock table (58 words, purpose TBD) */
extern uint16_t FILE_$LOCK_TABLE2[];

/*
 * ============================================================================
 * Internal Helper Functions
 * ============================================================================
 */

/*
 * OS_PROC_SHUTWIRED - Shutdown wired pages (called on access denial)
 *
 * Called when access rights check fails. Purpose is to release
 * any wired pages and resources.
 *
 * Parameters:
 *   status - Output status code
 *
 * Original address: 0x00E5D050
 */
extern void OS_PROC_SHUTWIRED(status_$t *status);

/*
 * FILE_$DELETE_INT - Internal delete handler
 *
 * Declared in file.h, used internally for refcount operations.
 */

#endif /* FILE_INTERNAL_H */
