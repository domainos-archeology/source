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

/*
 * REM_FILE_$NEIGHBORS - Check if two files are on same volume
 *
 * @param location_info  Location info for remote node
 * @param uid1           First file UID
 * @param uid2           Second file UID
 * @param status         Output status code
 *
 * Returns: Non-zero if files are on same volume
 *
 * Original address: 0x00E621B8
 */
int8_t REM_FILE_$NEIGHBORS(void *location_info, uid_t *uid1, uid_t *uid2,
                           status_$t *status);

/*
 * REM_FILE_$PURIFY - Flush remote file pages to disk
 *
 * @param vol_uid        Volume UID
 * @param file_uid       File UID
 * @param flags          Purify flags
 * @param page_index     Page index for partial purify
 * @param status         Output status code
 *
 * Original address: 0x00E6225C
 */
void REM_FILE_$PURIFY(uid_t *vol_uid, uid_t *file_uid, uint16_t *flags,
                      int16_t page_index, status_$t *status);

/*
 * REM_FILE_$SET_ATTRIBUTE - Set attribute on remote file
 *
 * @param vol_uid        Volume UID
 * @param file_uid       File UID
 * @param attr_id        Attribute ID
 * @param attr_data      Attribute data
 * @param status         Output status code
 *
 * Original address: 0x00E61A32
 */
void REM_FILE_$SET_ATTRIBUTE(void *vol_uid, uid_t *file_uid,
                             uint16_t attr_id, void *attr_data,
                             status_$t *status);

/*
 * REM_FILE_$LOCK - Lock a remote file
 *
 * @param location_block Location block
 * @param lock_mode      Lock mode
 * @param lock_type      Lock type
 * @param flags          Flags
 * @param wait_flag      Wait flag
 * @param extended       Extended lock flag (negative for extended)
 * @param lock_key       Lock key
 * @param packet_id_out  Output packet ID
 * @param status_word    Output status word
 * @param lock_result    Output lock result
 * @param status         Output status code
 *
 * Original address: 0x00E61AAE
 */
void REM_FILE_$LOCK(void *location_block, uint16_t lock_mode, uint16_t lock_type,
                    uint16_t flags, uint16_t wait_flag, int8_t extended,
                    uint32_t lock_key, uint16_t *packet_id_out, uint16_t *status_word,
                    void *lock_result, status_$t *status);

/*
 * REM_FILE_$UNLOCK - Unlock a remote file
 *
 * @param location_block Location block
 * @param unlock_mode    Unlock mode
 * @param lock_key       Lock key
 * @param wait_flag      Wait flag
 * @param remote_node    Remote node holding lock
 * @param release_flag   Release flag
 * @param status         Output status code
 *
 * Returns: Result byte from unlock operation
 *
 * Original address: 0x00E61D1C
 */
uint8_t REM_FILE_$UNLOCK(void *location_block, uint16_t unlock_mode,
                         uint32_t lock_key, uint16_t wait_flag,
                         uint32_t remote_node, int16_t release_flag,
                         status_$t *status);

/*
 * REM_FILE_$LOCAL_VERIFY - Verify lock on remote file server
 *
 * @param addr_info      Address info for remote node
 * @param lock_block     Lock block to verify
 * @param status         Output status code
 *
 * Original address: 0x00E61E20
 */
void REM_FILE_$LOCAL_VERIFY(void *addr_info, void *lock_block, status_$t *status);

/*
 * REM_FILE_$TEST - Test connectivity to remote file server
 *
 * @param addr_info      Address info for remote node
 * @param status         Output status code
 *
 * Original address: 0x00E62374
 */
void REM_FILE_$TEST(void *addr_info, status_$t *status);

/*
 * REM_FILE_$SET_DEF_ACL - Set default ACL on remote directory
 *
 * @param vol_uid        Volume UID
 * @param dir_uid        Directory UID
 * @param acl_uid        ACL UID
 * @param owner_uid      Owner UID
 * @param status         Output status code
 *
 * Original address: 0x00E622EE
 */
void REM_FILE_$SET_DEF_ACL(void *vol_uid, uid_t *dir_uid, uid_t *acl_uid,
                           uid_t *owner_uid, status_$t *status);

/*
 * REM_FILE_$CREATE_AREA - Create an area in a remote file
 *
 * @param addr_info      Address info for remote node
 * @param area_type      Area type
 * @param area_size      Area size
 * @param area_offset    Area offset
 * @param flags          Flags
 * @param pkt_size_out   Output packet size
 * @param status         Output status code
 *
 * Returns: Area handle
 *
 * Original address: 0x00E62622
 */
uint16_t REM_FILE_$CREATE_AREA(void *addr_info, uint32_t area_type,
                               uint32_t area_size, uint32_t area_offset,
                               uint8_t flags, uint16_t *pkt_size_out,
                               status_$t *status);

/*
 * REM_FILE_$DELETE_AREA - Delete an area in a remote file
 *
 * @param addr_info      Address info for remote node
 * @param area_handle    Area handle
 * @param area_offset    Area offset
 * @param status         Output status code
 *
 * Original address: 0x00E626CC
 */
void REM_FILE_$DELETE_AREA(void *addr_info, uint16_t area_handle,
                           uint32_t area_offset, status_$t *status);

/*
 * REM_FILE_$GROW_AREA - Grow an area in a remote file
 *
 * @param addr_info      Address info for remote node
 * @param area_handle    Area handle
 * @param current_size   Current area size
 * @param new_size       New area size
 * @param status         Output status code
 *
 * Original address: 0x00E62734
 */
void REM_FILE_$GROW_AREA(void *addr_info, uint16_t area_handle,
                         uint32_t current_size, uint32_t new_size,
                         status_$t *status);

/*
 * REM_FILE_$LOCAL_READ_LOCK - Read lock info from remote server
 *
 * @param addr_info      Address info for remote node
 * @param lock_data      Lock data to query
 * @param result_out     Output result byte
 * @param status         Output status code
 *
 * Original address: 0x00E61E9A
 */
void REM_FILE_$LOCAL_READ_LOCK(void *addr_info, void *file_uid,
                                void *lock_info_out, status_$t *status);

/*
 * REM_FILE_$NAME_ADD_HARD_LINKU - Add hard link on remote server
 *
 * @param addr_info      Address info for remote node
 * @param dir_uid        Directory UID
 * @param name           Entry name
 * @param name_len       Name length
 * @param file_uid       File UID to link to
 * @param status         Output status code
 *
 * Original address: 0x00E624EC
 */
void REM_FILE_$NAME_ADD_HARD_LINKU(void *addr_info, uid_t *dir_uid,
                                    char *name, uint16_t name_len,
                                    uid_t *file_uid, status_$t *status);

/*
 * REM_FILE_$DROP_HARD_LINKU - Remove hard link on remote server
 *
 * @param addr_info      Address info for remote node
 * @param dir_uid        Directory UID
 * @param name           Entry name
 * @param name_len       Name length
 * @param flags          Additional flags
 * @param status         Output status code
 *
 * Original address: 0x00E62588
 */
void REM_FILE_$DROP_HARD_LINKU(void *addr_info, uid_t *dir_uid,
                                char *name, uint16_t name_len,
                                uint16_t flags, status_$t *status);

/*
 * REM_FILE_$ACL_IMAGE - Get ACL image from remote file
 *
 * @param addr_info      Address info for remote node
 * @param file_uid       File UID
 * @param acl_type       ACL type
 * @param bulk_data_out  Output buffer for ACL data
 * @param acl_len_out    Output ACL length
 * @param acl_header_out Output ACL header (11 uint32s)
 * @param status         Output status code
 *
 * Original address: 0x00E627A8
 */
void REM_FILE_$ACL_IMAGE(void *addr_info, uid_t *file_uid,
                         uint8_t acl_type, void *bulk_data_out,
                         uint16_t *acl_len_out, void *acl_header_out,
                         status_$t *status);

/*
 * REM_FILE_$ACL_CREATE - Create ACL on remote file
 *
 * @param addr_info      Address info for remote node
 * @param acl_data       ACL data (up to 1KB)
 * @param acl_header     ACL header (11 uint32s)
 * @param parent_uid     Parent UID
 * @param acl_uid_out    Output ACL UID
 * @param status         Output status code
 *
 * Original address: 0x00E6283C
 */
void REM_FILE_$ACL_CREATE(void *addr_info, void *acl_data,
                          void *acl_header, uid_t *parent_uid,
                          uid_t *acl_uid_out, status_$t *status);

/*
 * REM_FILE_$ACL_SETIDS - Set IDs in remote ACL
 *
 * @param addr_info      Address info for remote node
 * @param acl_uid        ACL UID
 * @param sid_data       SID data (9 uint32s)
 * @param owner_data     Owner data (3 uint32s)
 * @param modified_flag_out Output modified flag
 * @param status         Output status code
 *
 * Original address: 0x00E62930
 */
void REM_FILE_$ACL_SETIDS(void *addr_info, uid_t *acl_uid,
                          void *sid_data, void *owner_data,
                          int8_t *modified_flag_out, status_$t *status);

/*
 * REM_FILE_$ACL_CHECK_RIGHTS - Check ACL rights on remote file
 *
 * @param addr_info      Address info for remote node
 * @param sid_data       SID data (9 uint32s)
 * @param perm_data      Permission data (16 uint32s)
 * @param file_uid       File UID
 * @param check_flag     Check flag
 * @param access_mask    Access mask to check
 * @param flags          Flags
 * @param flag2          Flag 2
 * @param flag3          Flag 3
 * @param result_out     Output result
 * @param status         Output status code
 *
 * Original address: 0x00E629E8
 */
void REM_FILE_$ACL_CHECK_RIGHTS(void *addr_info, void *sid_data,
                                 void *perm_data, uid_t *file_uid,
                                 uint8_t check_flag, uint32_t access_mask,
                                 uint16_t flags, uint8_t flag2, uint8_t flag3,
                                 uint32_t *result_out, status_$t *status);

/*
 * REM_FILE_$SET_ACL - Set ACL on remote file
 *
 * @param addr_info      Address info for remote node
 * @param file_uid       File UID
 * @param acl_uid        ACL UID
 * @param acl_header     ACL header (11 uint32s)
 * @param sid_data       SID data (9 uint32s)
 * @param perm_data      Permission data (16 uint32s)
 * @param flags          Flags
 * @param extra_flags    Extra flags
 * @param status         Output status code
 *
 * Original address: 0x00E62AA8
 */
void REM_FILE_$SET_ACL(void *addr_info, uid_t *file_uid, uid_t *acl_uid,
                       void *acl_header, void *sid_data, void *perm_data,
                       uint16_t flags, uint16_t extra_flags,
                       status_$t *status);

/*
 * REM_FILE_$FILE_SET_PROT - Set file protection on remote file server
 *
 * @param addr_info      Address info for remote node
 * @param file_uid       File UID
 * @param prot_data1     Protection data block 1 (13 uint32s)
 * @param flags          Flags
 * @param prot_data2     Protection data block 2 (25 uint32s)
 * @param flag           Flag byte
 * @param mtime_out      Output modification time
 * @param status         Output status code
 *
 * Original address: 0x00E62B64
 */
void REM_FILE_$FILE_SET_PROT(void *addr_info, uid_t *file_uid,
                              void *prot_data1, uint16_t flags,
                              void *prot_data2, uint8_t flag,
                              clock_t *mtime_out, status_$t *status);

/*
 * REM_FILE_$GET_SEG_MAP - Get segment map from remote file
 *
 * @param addr_info      Address info for remote node
 * @param file_uid       File UID
 * @param start_offset   Start offset
 * @param end_offset     End offset
 * @param type_flag      Type flag
 * @param seg_map_out    Output segment map
 * @param status         Output status code
 *
 * Original address: 0x00E61F3E
 */
void REM_FILE_$GET_SEG_MAP(void *addr_info, uid_t *file_uid,
                            uint32_t start_offset, uint32_t end_offset,
                            uint8_t type_flag, uint32_t *seg_map_out,
                            status_$t *status);

/*
 * REM_FILE_$NAME_GET_ENTRYU - Get directory entry by name from remote server
 *
 * @param addr_info      Address info for remote node
 * @param dir_uid        Directory UID
 * @param name           Entry name
 * @param name_len       Name length
 * @param result_out     Output result structure
 * @param status         Output status code
 *
 * Original address: 0x00E6209A
 */
void REM_FILE_$NAME_GET_ENTRYU(void *addr_info, uid_t *dir_uid,
                                char *name, uint16_t name_len,
                                void *result_out, status_$t *status);

/*
 * REM_FILE_$RN_DO_OP - Execute remote naming operation
 *
 * @param addr_info      Address info for remote node
 * @param op_buf         Operation buffer
 * @param base_len       Base request length
 * @param response_size  Response buffer size
 * @param response       Response buffer
 * @param extra_out      Extra output data
 *
 * Original address: 0x00E61538
 */
void REM_FILE_$RN_DO_OP(void *addr_info, void *op_buf,
                         int16_t base_len, uint16_t response_size,
                         void *response, void *extra_out);

/*
 * REM_FILE_$SERVER - Remote file operations server dispatcher
 *
 * This is the server-side handler for incoming remote file operation requests.
 * It receives requests from remote nodes, dispatches to the appropriate handler,
 * and sends back responses.
 *
 * The server handles approximately 30 different operation codes including:
 * - Lock/unlock operations
 * - File attribute operations
 * - Protection/ACL operations
 * - Directory operations
 * - Area management
 * - Node/process management
 *
 * Original address: 0x00E63586
 */
void REM_FILE_$SERVER(void);

#endif /* REM_FILE_H */
