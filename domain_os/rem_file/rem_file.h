/*
 * REM_FILE - Remote File Operations
 *
 * This module provides operations on remote (network) files.
 */

#ifndef REM_FILE_H
#define REM_FILE_H

#include "base/base.h"

/*
 * REM_FILE_$UNLOCK_ALL - Release all remote file locks
 *
 * Sends a network packet to all remote nodes to release any
 * file locks held by this node. Called during initialization
 * and cleanup to ensure no stale locks remain.
 *
 * Original address: 0x00E61C72
 */
void REM_FILE_$UNLOCK_ALL(void);

/*
 * REM_FILE_$RESERVE - Reserve space in a remote file
 *
 * @param vol_uid     Volume UID
 * @param uid         Object UID
 * @param start       Starting byte offset
 * @param count       Number of bytes to reserve
 * @param status      Output status code
 */
void REM_FILE_$RESERVE(uid_t *vol_uid, uid_t *uid, uint32_t start,
                       uint32_t count, status_$t *status);

/*
 * REM_FILE_$TRUNCATE - Truncate a remote file
 *
 * @param vol_uid     Volume UID
 * @param uid         Object UID
 * @param new_size    New file size
 * @param flags       Truncation flags
 * @param result      Output result byte
 * @param status      Output status code
 */
void REM_FILE_$TRUNCATE(uid_t *vol_uid, uid_t *uid, uint32_t new_size,
                        uint16_t flags, uint8_t *result, status_$t *status);

/*
 * REM_FILE_$INVALIDATE - Invalidate pages in a remote file
 *
 * @param vol_uid     Volume UID
 * @param uid         Object UID
 * @param start       Starting page offset
 * @param count       Number of pages to invalidate
 * @param flags       Invalidation flags
 * @param status      Output status code
 */
void REM_FILE_$INVALIDATE(uid_t *vol_uid, uid_t *uid, uint32_t start,
                          uint32_t count, int16_t flags, status_$t *status);

/*
 * REM_FILE_$FILE_SET_ATTRIB - Set attribute on remote file
 *
 * @param location_info  Location info from AST_$GET_LOCATION
 * @param file_uid       UID of file to modify
 * @param value          Pointer to attribute value
 * @param attr_id        Attribute ID
 * @param exsid          Extended SID from ACL_$GET_EXSID
 * @param required_rights Required access rights
 * @param option_flags   Option flags
 * @param clock_out      Output clock value
 * @param status         Output status code
 *
 * Original address: 0x00E62C22
 */
void REM_FILE_$FILE_SET_ATTRIB(void *location_info, uid_t *file_uid,
                               void *value, int16_t attr_id, void *exsid,
                               uint16_t required_rights, int16_t option_flags,
                               void *clock_out, status_$t *status);

/*
 * REM_FILE_$CREATE_TYPE - Create a file on a remote node
 *
 * Creates a new file on a remote node with the specified type and attributes.
 *
 * Parameters:
 *   parent_loc   - Location info for parent directory
 *   file_type    - Type of file to create (0-5)
 *   type_uid     - Type UID for typed objects
 *   size         - Initial file size
 *   flags        - Creation flags
 *   owner_uid    - Owner UID for the new file
 *   parent_attrs - Parent directory attributes
 *   location_out - Output buffer for new file location (includes file UID)
 *   status       - Output status code
 *
 * Original address: 0x00E6171A
 */
void REM_FILE_$CREATE_TYPE(uid_t *parent_loc, int16_t file_type,
                            uid_t *type_uid, uint32_t size,
                            uint16_t flags, uid_t *owner_uid,
                            void *parent_attrs, void *location_out,
                            status_$t *status);

/*
 * REM_FILE_$CREATE_TYPE_PRESR10 - Create a file on a pre-SR10 remote node
 *
 * Fallback creation function for older remote nodes that don't support
 * the full CREATE_TYPE protocol.
 *
 * Parameters:
 *   parent_loc - Location info for parent directory
 *   file_type  - Type of file to create
 *   is_dir     - Non-zero if creating a directory-like object
 *   file_uid   - Output: receives the new file's UID
 *   status     - Output status code
 *
 * Original address: 0x00E61868
 */
void REM_FILE_$CREATE_TYPE_PRESR10(uid_t *parent_loc, int16_t file_type,
                                    int16_t is_dir, uid_t *file_uid,
                                    status_$t *status);

#endif /* REM_FILE_H */
