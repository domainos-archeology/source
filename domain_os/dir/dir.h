/*
 * DIR - Directory Operations Module
 *
 * This module provides directory operations for Domain/OS including:
 * - Directory entry management (add, drop, rename)
 * - Hard and soft link operations
 * - Directory creation and deletion
 * - Protection and ACL operations
 * - Directory reading and traversal
 *
 * The DIR subsystem operates both locally and via remote procedure calls
 * to directory servers. Functions typically try the new protocol first,
 * then fall back to OLD_* variants for compatibility with older servers.
 *
 * Status codes are from module 0x0E (naming) and 0x0F (file).
 */

#ifndef DIR_H
#define DIR_H

#include "base/base.h"

/*
 * ============================================================================
 * Constants
 * ============================================================================
 */

/* Maximum pathname/leaf name lengths */
#define DIR_MAX_LEAF_LEN    255     /* Maximum leaf (filename) length */
#define DIR_MAX_PATH_LEN    1023    /* Maximum pathname length (0x3FF) */
#define DIR_MAX_LINK_LEN    1023    /* Maximum link target length */

/*
 * Directory operation codes (op field in request structures)
 */
#define DIR_OP_ADDU                 0x2A    /* Add directory entry */
#define DIR_OP_ADD_HARD_LINKU       0x2C    /* Add hard link */
#define DIR_OP_DROP_HARD_LINKU      0x2E    /* Drop hard link */
#define DIR_OP_CNAMEU               0x32    /* Change name (rename) */
#define DIR_OP_ADD_BAKU             0x34    /* Add backup entry */
#define DIR_OP_DELETE_FILEU         0x36    /* Delete file */
#define DIR_OP_CREATE_DIRU          0x38    /* Create directory */
#define DIR_OP_DROP_DIRU            0x3A    /* Drop directory */
#define DIR_OP_ADD_LINKU            0x3C    /* Add soft link */
#define DIR_OP_READ_LINKU           0x3E    /* Read soft link */
#define DIR_OP_DROP_LINKU           0x40    /* Drop soft link */
#define DIR_OP_FIND_UID             0x46    /* Find by UID */
#define DIR_OP_FIX_DIR              0x48    /* Fix directory */
#define DIR_OP_GET_DEFAULT_ACL      0x4E    /* Get default ACL */
#define DIR_OP_SET_DEFAULT_ACL      0x4C    /* Set default ACL */
#define DIR_OP_VALIDATE_ROOT_ENTRY  0x50    /* Validate root entry */
#define DIR_OP_SET_PROTECTION       0x52    /* Set protection */
#define DIR_OP_SET_DEF_PROTECTION   0x54    /* Set default protection */
#define DIR_OP_GET_DEF_PROTECTION   0x56    /* Get default protection */
#define DIR_OP_RESOLVE              0x58    /* Resolve pathname */
#define DIR_OP_DROP_MOUNT           0x5C    /* Drop mount point */

/*
 * ============================================================================
 * Public Function Prototypes
 * ============================================================================
 */

/*
 * DIR_$INIT - Initialize the directory subsystem
 *
 * Called during system boot to initialize directory services.
 *
 * Original address: 0x00E3140C
 */
void DIR_$INIT(void);

/*
 * DIR_$RESOLVE - Resolve a pathname relative to a directory
 *
 * Resolves a pathname string starting from a base directory UID.
 * Returns the resolved object's UID and type information.
 *
 * Parameters:
 *   pathname    - The pathname to resolve
 *   path_len    - Pointer to pathname length (max 1023)
 *   start_uid   - Starting directory UID (in/out - updated on partial resolution)
 *   resolved_uid - Output: UID of resolved object
 *   param5-8    - Various resolution parameters
 *   flags       - Resolution flags
 *   link_count  - Output: link nesting count
 *   status_ret  - Output: status code
 *
 * Original address: 0x00E4D356
 */
void DIR_$RESOLVE(void *pathname, uint16_t *path_len, uid_t *start_uid,
                  uid_t *resolved_uid, uint16_t *param5, uint16_t *param6,
                  uint16_t *param7, uint16_t *param8, void *flags,
                  uint16_t *link_count, status_$t *status_ret);

/*
 * DIR_$FIND_UID - Find a UID in a directory
 *
 * Searches a directory for an entry with the specified UID and returns
 * its name.
 *
 * Parameters:
 *   dir_uid     - UID of directory to search
 *   target_uid  - UID to find
 *   name_buf_len - Pointer to max buffer length
 *   name_buf    - Output: name of found entry
 *   param5      - Additional parameters
 *   status_ret  - Output: status code
 *
 * Original address: 0x00E4E87C
 */
void DIR_$FIND_UID(uid_t *dir_uid, uid_t *target_uid, uint16_t *name_buf_len,
                   char *name_buf, void *param5, status_$t *status_ret);

/*
 * DIR_$FIND_NET - Find network node for a directory entry
 *
 * Returns the high part of the network node address for the given index.
 *
 * Parameters:
 *   dir_uid - UID of directory
 *   index   - Pointer to index value (low 20 bits of UID)
 *
 * Returns:
 *   High part of node address
 *
 * Original address: 0x00E4E8B4
 */
uint32_t DIR_$FIND_NET(uid_t *dir_uid, uint32_t *index);

/*
 * DIR_$FIX_DIR - Fix/repair a directory
 *
 * Attempts to repair a corrupted directory structure.
 *
 * Parameters:
 *   dir_uid    - UID of directory to fix
 *   status_ret - Output: status code
 *
 * Original address: 0x00E53E84
 */
void DIR_$FIX_DIR(uid_t *dir_uid, status_$t *status_ret);

/*
 * DIR_$GET_DEF_PROTECTION - Get default protection for a directory
 *
 * Retrieves the default ACL/protection that will be applied to new
 * entries created in this directory.
 *
 * Parameters:
 *   dir_uid    - UID of directory
 *   acl_type   - ACL type UID
 *   prot_buf   - Output: protection data (44 bytes)
 *   prot_uid   - Output: protection UID
 *   status_ret - Output: status code
 *
 * Original address: 0x00E51D54
 */
void DIR_$GET_DEF_PROTECTION(uid_t *dir_uid, uid_t *acl_type,
                             void *prot_buf, uid_t *prot_uid,
                             status_$t *status_ret);

/*
 * DIR_$SET_DEF_PROTECTION - Set default protection for a directory
 *
 * Sets the default ACL/protection that will be applied to new
 * entries created in this directory.
 *
 * Parameters:
 *   dir_uid    - UID of directory
 *   acl_type   - ACL type UID
 *   prot_buf   - Protection data to set (44 bytes)
 *   prot_uid   - Protection UID
 *   status_ret - Output: status code
 *
 * Original address: 0x00E520A6
 */
void DIR_$SET_DEF_PROTECTION(uid_t *dir_uid, uid_t *acl_type,
                             void *prot_buf, uid_t *prot_uid,
                             status_$t *status_ret);

/*
 * DIR_$SET_PROTECTION - Set protection on a file
 *
 * Sets the ACL/protection on a specific file.
 *
 * Parameters:
 *   file_uid   - UID of file
 *   prot_buf   - Protection data to set (44 bytes)
 *   acl_uid    - ACL UID
 *   prot_type  - Pointer to protection type (4, 5, or 6)
 *   status_ret - Output: status code
 *
 * Original address: 0x00E52228
 */
void DIR_$SET_PROTECTION(uid_t *file_uid, void *prot_buf, uid_t *acl_uid,
                         int16_t *prot_type, status_$t *status_ret);

/*
 * DIR_$GET_DEFAULT_ACL - Get default ACL for a directory
 *
 * Retrieves the default ACL UID for the directory.
 *
 * Parameters:
 *   dir_uid    - UID of directory
 *   acl_type   - ACL type UID
 *   acl_ret    - Output: default ACL UID
 *   status_ret - Output: status code
 *
 * Original address: 0x00E531C8
 */
void DIR_$GET_DEFAULT_ACL(uid_t *dir_uid, uid_t *acl_type, uid_t *acl_ret,
                          status_$t *status_ret);

/*
 * DIR_$SET_DEFAULT_ACL - Set default ACL for a directory
 *
 * Sets the default ACL for new entries in the directory.
 *
 * Parameters:
 *   dir_uid    - UID of directory
 *   acl_type   - ACL type UID
 *   acl_uid    - ACL UID to set
 *   status_ret - Output: status code
 *
 * Original address: 0x00E53004
 */
void DIR_$SET_DEFAULT_ACL(uid_t *dir_uid, uid_t *acl_type, uid_t *acl_uid,
                          status_$t *status_ret);

/*
 * DIR_$SET_DAD - Set directory parent pointer
 *
 * Sets the parent directory pointer for a directory entry.
 * This is a thin wrapper around FILE_$SET_DIRPTR.
 *
 * Parameters:
 *   dir_uid    - UID of directory
 *   parent_uid - UID of parent directory
 *   status_ret - Output: status code
 *
 * Original address: 0x00E52B66
 */
void DIR_$SET_DAD(uid_t *dir_uid, uid_t *parent_uid, status_$t *status_ret);

/*
 * DIR_$VALIDATE_ROOT_ENTRY - Validate a root directory entry
 *
 * Verifies that an entry exists in the root directory.
 *
 * Parameters:
 *   name       - Name to validate
 *   name_len   - Pointer to name length (max 255)
 *   status_ret - Output: status code
 *
 * Original address: 0x00E5039A
 */
void DIR_$VALIDATE_ROOT_ENTRY(char *name, uint16_t *name_len,
                              status_$t *status_ret);

/*
 * DIR_$ADDU - Add a directory entry
 *
 * Adds a new entry to a directory.
 *
 * Parameters:
 *   dir_uid    - UID of parent directory
 *   name       - Name for the new entry
 *   name_len   - Pointer to name length (max 255)
 *   file_uid   - UID of file to add
 *   status_ret - Output: status code
 *
 * Original address: 0x00E501A0
 */
void DIR_$ADDU(uid_t *dir_uid, char *name, int16_t *name_len,
               uid_t *file_uid, status_$t *status_ret);

/*
 * DIR_$ADD_HARD_LINKU - Add a hard link
 *
 * Creates a hard link to an existing file.
 *
 * Parameters:
 *   dir_uid    - UID of directory for link
 *   name       - Name for the link
 *   name_len   - Pointer to name length (max 255)
 *   target_uid - UID of target file
 *   status_ret - Output: status code
 *
 * Original address: 0x00E505CA
 */
void DIR_$ADD_HARD_LINKU(uid_t *dir_uid, char *name, uint16_t *name_len,
                         uid_t *target_uid, status_$t *status_ret);

/*
 * DIR_$ADD_LINKU - Add a soft/symbolic link
 *
 * Creates a symbolic link pointing to a pathname.
 *
 * Parameters:
 *   dir_uid    - UID of directory for link
 *   name       - Name for the link
 *   name_len   - Pointer to name length (max 255)
 *   target     - Target pathname
 *   target_len - Pointer to target length (max 1023)
 *   status_ret - Output: status code
 *
 * Original address: 0x00E5068E
 */
void DIR_$ADD_LINKU(uid_t *dir_uid, char *name, int16_t *name_len,
                    void *target, uint16_t *target_len, status_$t *status_ret);

/*
 * DIR_$ADD_BAKU - Add a backup entry
 *
 * Creates a backup directory entry.
 *
 * Parameters:
 *   dir_uid    - UID of directory
 *   name       - Name for the backup
 *   name_len   - Pointer to name length (max 255)
 *   backup_uid - UID of backup file
 *   status_ret - Output: status code
 *
 * Original address: 0x00E50C60
 */
void DIR_$ADD_BAKU(uid_t *dir_uid, char *name, uint16_t *name_len,
                   uid_t *backup_uid, status_$t *status_ret);

/*
 * DIR_$DELETE_FILEU - Delete a file from directory
 *
 * Removes a file entry and optionally deletes the file.
 *
 * Parameters:
 *   dir_uid    - UID of parent directory
 *   name       - Name of entry to delete
 *   name_len   - Pointer to name length (max 255)
 *   param4     - Status output parameter
 *   param5     - Flags parameter
 *   status_ret - Output: status code
 *
 * Original address: 0x00E515BC
 */
void DIR_$DELETE_FILEU(uid_t *dir_uid, char *name, uint16_t *name_len,
                       status_$t *param4, void *param5, status_$t *status_ret);

/*
 * DIR_$DROPU - Drop a directory entry
 *
 * Removes an entry from a directory. Returns the UID of the dropped entry.
 *
 * Parameters:
 *   dir_uid    - UID of parent directory
 *   name       - Name of entry to drop
 *   name_len   - Pointer to name length
 *   file_uid   - Output: UID of dropped file (set to NIL)
 *   status_ret - Output: status code
 *
 * Original address: 0x00E516C2
 */
void DIR_$DROPU(uid_t *dir_uid, char *name, uint16_t *name_len,
                uid_t *file_uid, status_$t *status_ret);

/*
 * DIR_$DROP_HARD_LINKU - Drop a hard link
 *
 * Removes a hard link from a directory.
 *
 * Parameters:
 *   dir_uid    - UID of parent directory
 *   name       - Name of link to drop
 *   name_len   - Pointer to name length
 *   flags      - Pointer to flags
 *   status_ret - Output: status code
 *
 * Original address: 0x00E516FC
 */
void DIR_$DROP_HARD_LINKU(uid_t *dir_uid, char *name, uint16_t *name_len,
                          uint16_t *flags, status_$t *status_ret);

/*
 * DIR_$DROP_LINKU - Drop a soft link
 *
 * Removes a symbolic link from a directory.
 *
 * Parameters:
 *   dir_uid    - UID of parent directory
 *   name       - Name of link to drop
 *   name_len   - Pointer to name length
 *   target_uid - Output: UID associated with link
 *   status_ret - Output: status code
 *
 * Original address: 0x00E517F6
 */
void DIR_$DROP_LINKU(uid_t *dir_uid, char *name, uint16_t *name_len,
                     uid_t *target_uid, status_$t *status_ret);

/*
 * DIR_$CNAMEU - Change name (rename) an entry
 *
 * Renames a directory entry from one name to another.
 *
 * Parameters:
 *   dir_uid      - UID of directory containing entry
 *   old_name     - Current name
 *   old_name_len - Pointer to current name length
 *   new_name     - New name
 *   new_name_len - Pointer to new name length
 *   status_ret   - Output: status code
 *
 * Original address: 0x00E51B68
 */
void DIR_$CNAMEU(uid_t *dir_uid, char *old_name, uint16_t *old_name_len,
                 char *new_name, uint16_t *new_name_len, status_$t *status_ret);

/*
 * DIR_$CREATE_DIRU - Create a subdirectory
 *
 * Creates a new directory as a child of the given directory.
 *
 * Parameters:
 *   parent_uid - UID of parent directory
 *   name       - Name for new directory
 *   name_len   - Pointer to name length
 *   new_dir_uid - Output: UID of created directory
 *   status_ret - Output: status code
 *
 * Original address: 0x00E529EE
 */
void DIR_$CREATE_DIRU(uid_t *parent_uid, char *name, uint16_t *name_len,
                      uid_t *new_dir_uid, status_$t *status_ret);

/*
 * DIR_$DROP_DIRU - Drop/delete a directory
 *
 * Removes a directory entry from its parent.
 *
 * Parameters:
 *   parent_uid - UID of parent directory
 *   name       - Name of directory to drop
 *   name_len   - Pointer to name length
 *   status_ret - Output: status code
 *
 * Original address: 0x00E52AB4
 */
void DIR_$DROP_DIRU(uid_t *parent_uid, char *name, uint16_t *name_len,
                    status_$t *status_ret);

/*
 * DIR_$ROOT_ADDU - Add entry to root directory
 *
 * Adds an entry to the root directory with additional flags.
 *
 * Parameters:
 *   dir_uid    - UID of root directory
 *   name       - Name for entry
 *   name_len   - Pointer to name length
 *   file_uid   - UID of file to add
 *   flags      - Pointer to flags
 *   status_ret - Output: status code
 *
 * Original address: 0x00E52B8C
 */
void DIR_$ROOT_ADDU(uid_t *dir_uid, char *name, uint16_t *name_len,
                    uid_t *file_uid, uint32_t *flags, status_$t *status_ret);

/*
 * DIR_$GET_ENTRYU - Get a directory entry by name
 *
 * Retrieves information about a named entry in a directory.
 *
 * Parameters:
 *   dir_uid    - UID of directory to search
 *   name       - Name to look up
 *   name_len   - Pointer to name length (max 255)
 *   entry_ret  - Output: entry information
 *   status_ret - Output: status code
 *
 * Original address: 0x00E4D500
 */
void DIR_$GET_ENTRYU(uid_t *dir_uid, char *name, uint16_t *name_len,
                     void *entry_ret, status_$t *status_ret);

/*
 * DIR_$READ_LINKU - Read a symbolic link
 *
 * Reads the target pathname of a symbolic link.
 *
 * Parameters:
 *   dir_uid    - UID of directory containing link
 *   name       - Name of link
 *   name_len   - Pointer to name length
 *   buf_len    - Pointer to buffer length
 *   target     - Target pathname buffer
 *   target_len - Output: actual target length
 *   target_uid - Output: target UID
 *   status_ret - Output: status code
 *
 * Original address: 0x00E4D6C0
 */
void DIR_$READ_LINKU(uid_t *dir_uid, char *name, uint16_t *name_len,
                     int16_t *buf_len, void *target, uint16_t *target_len,
                     uid_t *target_uid, status_$t *status_ret);

/*
 * DIR_$DIR_READU - Read directory entries
 *
 * Reads entries from a directory starting at the given position.
 *
 * Parameters:
 *   dir_uid       - UID of directory to read
 *   entries_ret   - Output: array of directory entries
 *   entries_size  - Size of entries buffer
 *   continuation  - In/out: continuation position
 *   max_entries   - Pointer to max entries to return
 *   count_ret     - Pointer to count of entries returned
 *   flags         - Read flags
 *   eof_ret       - Pointer to EOF indicator
 *   status_ret    - Output: status code
 *
 * Original address: 0x00E4E3A6
 */
void DIR_$DIR_READU(uid_t *dir_uid, void *entries_ret, void *entries_size,
                    int32_t *continuation, uint16_t *max_entries,
                    int32_t *count_ret, void *flags, int32_t *eof_ret,
                    status_$t *status_ret);

/*
 * DIR_$DO_OP - Execute a directory operation
 *
 * Common entry point for directory operations. Sends request to
 * directory server and processes response.
 *
 * Parameters:
 *   request    - Request structure (varies by operation)
 *   req_size   - Size of request data
 *   resp_size  - Expected response size
 *   response   - Output: response structure
 *   resp_buf   - Additional response buffer
 *
 * Original address: 0x00E4C02C
 */
void DIR_$DO_OP(void *request, int16_t req_size, int16_t resp_size,
                void *response, void *resp_buf);

/*
 * DIR_$DROP_MOUNT - Remove a volume mount point from a directory
 *
 * Removes a mount point entry that was created when a logical volume
 * was mounted. Called during volume dismount to clean up the directory
 * entry linking to the volume's root.
 *
 * Parameters:
 *   mount_point_uid - UID of the mount point directory
 *   dir_uid         - UID of directory containing the mount entry
 *   lv_num          - Pointer to logical volume number (or zero)
 *   status_ret      - Output: status code
 *
 * Original address: 0x00E53518
 */
void DIR_$DROP_MOUNT(uid_t *mount_point_uid, uid_t *dir_uid, uint32_t *lv_num,
                     status_$t *status_ret);

#endif /* DIR_H */
