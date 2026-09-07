/*
 * FILE_$DELETE_INT - Internal file delete handler
 *
 * Original address: 0x00E5E8E0
 *
 * This is the core function that handles file deletion. All public
 * delete functions (FILE_$DELETE, FILE_$DELETE_FORCE, etc.) call this.
 *
 * The function:
 * 1. Hashes the file UID to find it in the lock table
 * 2. Takes the file lock (ML lock 5)
 * 3. Searches the lock entry chain for the file
 * 4. If file is found (locked):
 *    - If force mode (flags & 2), either set delete-on-unlock or return error
 * 5. If file not found (not locked):
 *    - Call internal functions and AST_$TRUNCATE to delete the file
 * 6. Releases the file lock
 */

#include "file/file_internal.h"
#include "ml/ml.h"
#include "uid/uid.h"
#include "ast/ast.h"

/* Status codes */
#define status_$ok                          0
#define file_$object_in_use          0x000F0006
#define file_$object_is_remote       0x000F0002
#define status_$ast_refcnt_says_unused      0x00030007

/* File lock ID */
#define FILE_LOCK_ID    5

/* Hash table size for lock table - at 0xe5ea28, value is 58 */
static const uint16_t FILE_LOCK_HASH_SIZE = 58;

/* Internal helper functions for per-UID locking during delete */
/* FILE_$UID_LOCK_ACQUIRE at 0x00E5D0A8 - declared in file_internal.h */
/* FILE_$UID_LOCK_RELEASE at 0x00E5D134 - declared in file_internal.h */

/*
 * FILE_$DELETE_INT
 *
 * Parameters:
 *   file_uid   - UID of file to delete
 *   flags      - Delete flags:
 *                  bit 0 (0x1): Do the delete operation
 *                  bit 1 (0x2): Force mode - don't error on locked file
 *                  bit 2 (0x4): Set delete-on-unlock if file is locked
 *   result     - Receives result byte
 *   status_ret - Receives operation status
 *
 * Returns:
 *   -1 (0xFF) if file was found in lock table (locked)
 *   0 if file was not found (not locked)
 *
 * Assembly at 0x00E5E8E0:
 *   link.w A6,-0x48
 *   movem.l { A4 A3 A2 D5 D4 D3 D2},-(SP)
 *   ...
 */
int8_t FILE_$DELETE_INT(uid_t *file_uid, uint16_t flags, uint8_t *result, status_$t *status_ret)
{
    int16_t hash_index;
    int16_t entry_index;
    int8_t found_locked;
    uint16_t truncate_flags;
    uint16_t attr_value[28];  /* Local buffer for FILE_$SET_ATTRIBUTE */

    /* Clear result output */
    *result = 0;
    found_locked = 0;

    /*
     * Hash the file UID to find the lock table bucket.
     * UID_$HASH returns: high 16 bits = quotient, low 16 bits = remainder (index)
     */
    hash_index = (int16_t)UID_$HASH(file_uid, (uint16_t *)&FILE_LOCK_HASH_SIZE);

    /* Take the file lock */
    ML_$LOCK(FILE_LOCK_ID);

    /*
     * Search the lock entry chain for this file.
     * The lock_map array at offset 0xc8 in the control block contains
     * the head of each hash bucket's chain.
     */
    entry_index = FILE_$LOCK_CONTROL.lock_map[hash_index];

    while (entry_index > 0) {
        /*
         * 0x00E5E92C-0x00E5E942: `lsl.l #0x2` / `neg` / `lsl.l #0x3` / `add` is
         * entry_index*0x1C added to 0xE935CC, which addresses the END of the
         * entry - so the UID is read at -0x10 (field +0x0C) and the chain link
         * at -0x08 (field +0x14).  FILE_$LOT_ENTRY(n) is the same entry
         * addressed from its start.
         */
        file_lock_entry_detail_t *entry = FILE_$LOT_ENTRY(entry_index);

        if (entry->uid_high == file_uid->high &&
            entry->uid_low  == file_uid->low) {
            found_locked = -1;  /* 0x00E5E954 st D3b - the file is locked */
            break;
        }

        entry_index = (int16_t)entry->next;         /* 0x00E5E958 */
    }

    /* Now handle based on flags */
    *status_ret = status_$ok;

    if (flags & 0x1) {
        /* Bit 0 set: Do the delete operation */

        if (found_locked < 0) {
            /* File is locked */
            if (!(flags & 0x2)) {
                /* Not force mode - return error */
                *status_ret = file_$object_in_use;
            } else if (flags & 0x4) {
                /* Force mode with delete-on-unlock flag */
                /* Call FILE_$SET_ATTRIBUTE to set attribute 7 (delete-on-unlock) */
                attr_value[0] = 1;
                FILE_$SET_ATTRIBUTE(file_uid, 7, attr_value,
                                    FILE_ATTR_RIGHTS_NONE, FILE_ATTR_OPTS_ALL,
                                    status_ret);

                /* These specific status codes are acceptable */
                if (*status_ret == file_$object_is_remote ||
                    *status_ret == status_$ast_refcnt_says_unused) {
                    *status_ret = status_$ok;
                }
            }
        } else {
            /* File is not locked - proceed with delete */

            /* Acquire per-UID lock to protect against concurrent ops */
            FILE_$UID_LOCK_ACQUIRE(file_uid);

            /* Release ML lock 5 while calling AST_$TRUNCATE */
            ML_$UNLOCK(FILE_LOCK_ID);

            /* Determine truncate flags */
            truncate_flags = 1;
            if (flags & 0x4) {
                truncate_flags = 3;  /* Delete with force */
            }

            /* Call AST_$TRUNCATE to actually delete the file */
            AST_$TRUNCATE(file_uid, 0, truncate_flags, result, status_ret);

            /* Re-acquire ML lock 5 */
            ML_$LOCK(FILE_LOCK_ID);

            /* Release per-UID lock and wake any waiters */
            FILE_$UID_LOCK_RELEASE(file_uid);
        }
    }

    /* Release the file lock */
    ML_$UNLOCK(FILE_LOCK_ID);

    return found_locked;
}

